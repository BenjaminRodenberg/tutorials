from jinja2 import Environment, select_autoescape, FileSystemLoader
import pandas as pd
from pathlib import Path
import subprocess
import datetime
import os
import uuid


default_precice_config_params = {
    'max_used_iterations': 10,
    'time_windows_reused': 5,
}


def render(precice_config_params):
    base_path = Path(__file__).parent.absolute()

    env = Environment(
        loader=FileSystemLoader(base_path),
        autoescape=select_autoescape(['xml'])
    )

    precice_config_template = env.get_template('precice-config-template.xml')

    precice_config_name = base_path / "precice-config.xml"

    with open(precice_config_name, "w") as file:
        file.write(precice_config_template.render(precice_config_params))


def do_run(dt, n_substeps = 1, error_tol=10e-3, precice_config_params=default_precice_config_params):
    time_window_size = dt
    time_step_size = time_window_size / n_substeps

    fenics = Path(__file__).parent.absolute() / "fenics"
    precice_config_params['time_window_size'] = time_window_size
    render(precice_config_params)
    print(f"{datetime.datetime.now()}: Start run with parameters {precice_config_params}")
    print("Running...")

    participants = [
        {
            "name": "Dirichlet",
            "cmd":"-d",
        },
        {
            "name": "Neumann",
            "cmd":"-n",
        },
    ]

    for participant in participants: participant['logfile'] = f"stdout-{participant['name']}.log"

    for participant in participants:
        with open(fenics / participant['logfile'], "w") as outfile:
            p = subprocess.Popen(["python3", fenics / "heat.py", participant["cmd"], f"-e {error_tol}", f"-s {n_substeps}"], cwd=fenics, stdout=outfile)
            participant["proc"] = p

    for participant in participants:
        participant["proc"].wait()

    for participant in participants:
        if participant["proc"].returncode != 0:
            raise Exception(f'Experiment failed. See logs {[p["logfile"] for p in participants]}')

    print("Done.")
    print("Postprocessing...")
    summary = {"time window size": time_window_size}
    for participant in participants:
        df = pd.read_csv(fenics / f"errors-{participant['name']}.csv", comment="#")
        summary[f"time step size {participant['name']}"] = time_step_size
        summary[f"error {participant['name']}"] = df["errors"].abs().max()
    print("Done.")

    return summary


if __name__ == "__main__":
    min_dt = 0.1
    dts = [min_dt * 0.5**i for i in range(5)]

    df = pd.DataFrame()

    precice_config_params = {
        'max_used_iterations': 10,
        'time_windows_reused': 5,
    }

    summary_file = Path("convergence-studies") / f"{uuid.uuid4()}.csv"

    for dt in dts:
        for n in [1]:
            summary = do_run(dt, n_substeps=n, error_tol=10e10, precice_config_params=precice_config_params)
            df = pd.concat([df, pd.DataFrame(summary, index=[0])], ignore_index=True)

            print(f"Write preliminary output to {summary_file}")
            df.to_csv(summary_file)

            term_size = os.get_terminal_size()
            print('-' * term_size.columns)
            print(df)
            print('-' * term_size.columns)

    df = df.set_index(['time window size', 'time step size Dirichlet', 'time step size Neumann'])
    print(f"Write final output to {summary_file}")

    import git

    repo_base = Path(__file__).parent / ".."
    repo = git.Repo(repo_base)
    chash = str(repo.head.commit)[:7]
    if repo.is_dirty():
        chash += "-dirty"

    metadata={
        "git repository": repo.remotes.origin.url,
        "git commit": chash,
        "precice_config_params": precice_config_params,
    }

    summary_file.unlink()

    with open(summary_file, 'a') as f:
        for key, value in metadata.items():
            f.write(f"# {key}:{value}\n")
        df.to_csv(f)

    print('-' * term_size.columns)
    print(df)
    print('-' * term_size.columns)