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


def do_run(dt, error_tol=10e-3, precice_config_params=default_precice_config_params):
    fenics = Path(__file__).parent.absolute() / "fenics"
    precice_config_params['time_window_size'] = dt
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
            p = subprocess.Popen(["python3", fenics / "heat.py", participant["cmd"], f"-e {error_tol}"], cwd=fenics, stdout=outfile)
            participant["proc"] = p

    for participant in participants:
        participant["proc"].wait()

    for participant in participants:
        if participant["proc"].returncode != 0:
            raise Exception(f'Experiment with dt={dt} failed. See logs {[p["logfile"] for p in participants]}')

    print("Done.")
    print("Postprocessing...")
    summary = {"dt":dt}
    for participant in participants:
        df = pd.read_csv(fenics / f"errors-{participant['name']}.csv", comment="#")
        summary[f"error {participant['name']}"] = df["errors"].abs().max()
    print("Done.")

    return summary


if __name__ == "__main__":
    min_dt = 0.1
    dts = [min_dt * 0.5**i for i in range(5)]

    df = pd.DataFrame(columns=["dt", "error Dirichlet", "error Neumann"])

    precice_config_params = {
        'max_used_iterations': 10,
        'time_windows_reused': 5,
    }

    summary_file = f"convergence-studies/{uuid.uuid4()}.csv"

    for dt in dts:
        summary = do_run(dt, error_tol=10e10, precice_config_params=precice_config_params)
        df = pd.concat([df, pd.DataFrame(summary, index=[0])], ignore_index=True)

        print(f"Write preliminary output to {summary_file}")
        df.to_csv(summary_file)

        term_size = os.get_terminal_size()
        print('-' * term_size.columns)
        print(df)
        print('-' * term_size.columns)

    df = df.set_index('dt')
    print(f"Write final output to {summary_file}")
    df.to_csv(summary_file)
    print('-' * term_size.columns)
    print(df)
    print('-' * term_size.columns)