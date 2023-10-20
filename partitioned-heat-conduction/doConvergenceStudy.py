from jinja2 import Environment, select_autoescape, FileSystemLoader
import pandas as pd
from pathlib import Path
import subprocess
import datetime
import os


def render(dt):
    base_path = Path(__file__).parent.absolute()

    env = Environment(
        loader=FileSystemLoader(base_path),
        autoescape=select_autoescape(['xml'])
    )

    precice_config_template = env.get_template('precice-config-template.xml')

    precice_config_name = base_path / "precice-config.xml"

    with open(precice_config_name, "w") as file:
        file.write(precice_config_template.render(time_window_size=dt))


def do_run(dt, error_tol=10e-3):
    fenics = Path(__file__).parent.absolute() / "fenics"
    render(dt=dt)
    print(f"Start run with dt={dt} at {datetime.datetime.now()} ...")
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

    for participant in participants:
        with open(fenics / f"stdout-{participant['name']}.log", "w") as outfile:
            p = subprocess.Popen(["python3", fenics / "heat.py", participant["cmd"], f"-e {error_tol}"], cwd=fenics, stdout=outfile)
            participant["proc"] = p

    for participant in participants:
        participant["proc"].wait()

    for participant in participants:
        if participant["proc"].returncode != 0:
            raise Exception(f'Experiment with dt={dt} failed!')

    print("Done.")
    print("Postprocessing...")
    summary = {"dt":dt}
    for participant in participants:
        df = pd.read_csv(fenics / f"errors-{participant['name']}.csv", comment="#")
        summary[f"error {participant['name']}"] = df["errors"].abs().max()
    print("Done.")

    return summary


if __name__ == "__main__":
    min_dt = 0.01
    dts = [min_dt * 0.5**i for i in range(10)]

    df = pd.DataFrame(columns=["dt", "error Dirichlet", "error Neumann"])

    summary_file = "convergence_study.csv"
    for dt in dts:
        summary = do_run(dt, error_tol=10e10)
        df = pd.concat([df, pd.DataFrame(summary, index=[0])], ignore_index=True)

        print(f"Write output to {summary_file}")
        df.to_csv(summary_file)

        term_size = os.get_terminal_size()
        print('-' * term_size.columns)
        print("Preliminary results:")
        print(df)
        print('-' * term_size.columns)