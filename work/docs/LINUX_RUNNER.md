# Linux Self-Hosted Runner

This document wires a Linux GitHub Actions runner so `TVectorBlur` can build
real CUDA `.so` binaries and run the Nuke runtime smoke test.

## Target Layout

- Runner labels: `self-hosted`, `nuke`, `linux`
- CUDA toolkit: `/usr/local/cuda`
- Nuke installs:
  - `/opt/Nuke13.0vX`
  - `/opt/Nuke14.0vX`
  - `/opt/Nuke15.0vX`
  - `/opt/Nuke16.0vX`
  - `/opt/Nuke17.0vX`
- GitHub runner dir example: `/opt/actions-runner/tvectorblur-linux`

## 1. Host Prerequisites

Install the base toolchain:

```bash
sudo apt-get update
sudo apt-get install -y build-essential git cmake ninja-build python3 python3-pip patchelf
```

Validate the NVIDIA stack:

```bash
nvidia-smi
nvcc --version
```

The runner needs:

- a recent NVIDIA driver
- a CUDA toolkit available to `nvcc`
- a Nuke install per version you want to compile

## 2. Create the Runner

Download the GitHub Actions runner from the repository settings page and unpack it:

```bash
sudo mkdir -p /opt/actions-runner/tvectorblur-linux
sudo chown "$USER":"$USER" /opt/actions-runner/tvectorblur-linux
cd /opt/actions-runner/tvectorblur-linux
tar xzf ~/actions-runner-linux-x64-*.tar.gz
```

Register it against the repo with the required labels:

```bash
./config.sh \
  --url https://github.com/Tom-Petroni/TVectorBlur \
  --token YOUR_EPHEMERAL_RUNNER_TOKEN \
  --labels self-hosted,nuke,linux \
  --unattended
```

## 3. Generate the Runner Environment

From the repo checkout:

```bash
cd /path/to/TVectorBlur/work
./scripts/write_linux_runner_env.sh \
  --output /opt/actions-runner/tvectorblur-linux/tvectorblur-runner.env \
  --cuda-root /usr/local/cuda \
  --nuke-root 16.0=/opt/Nuke16.0v9 \
  --nuke-root 17.0=/opt/Nuke17.0v1
```

This writes:

- `NUKE_16_0_ROOT=/opt/Nuke16.0v9`
- `NUKE_16_0_EXECUTABLE=/opt/Nuke16.0v9/Nuke16.0`
- `CUDA_HOME=/usr/local/cuda`
- `PATH=...`
- `LD_LIBRARY_PATH=...`

Validate the runner host:

```bash
./scripts/check_linux_runner.sh \
  --env-file /opt/actions-runner/tvectorblur-linux/tvectorblur-runner.env
```

## 4. Start the Runner With TVectorBlur Env

For a manual run:

```bash
/path/to/TVectorBlur/work/scripts/start_linux_runner_with_env.sh \
  /opt/actions-runner/tvectorblur-linux \
  /opt/actions-runner/tvectorblur-linux/tvectorblur-runner.env
```

For a persistent `systemd` service, create:

`/etc/systemd/system/tvectorblur-runner.service`

```ini
[Unit]
Description=TVectorBlur GitHub Actions Runner
After=network-online.target

[Service]
Type=simple
User=YOUR_LINUX_USER
WorkingDirectory=/opt/actions-runner/tvectorblur-linux
ExecStart=/path/to/TVectorBlur/work/scripts/start_linux_runner_with_env.sh /opt/actions-runner/tvectorblur-linux /opt/actions-runner/tvectorblur-linux/tvectorblur-runner.env
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now tvectorblur-runner.service
sudo systemctl status tvectorblur-runner.service
```

## 5. What GitHub Actions Reads

`nuke-build.yml` now resolves Nuke roots from runner environment variables:

- `NUKE_13_0_ROOT`
- `NUKE_14_0_ROOT`
- `NUKE_15_0_ROOT`
- `NUKE_16_0_ROOT`
- `NUKE_17_0_ROOT`

`nuke-runtime-smoke.yml` resolves the test executable from:

- `NUKE_13_0_EXECUTABLE`
- `NUKE_14_0_EXECUTABLE`
- `NUKE_15_0_EXECUTABLE`
- `NUKE_16_0_EXECUTABLE`
- `NUKE_17_0_EXECUTABLE`

The repo-level toggle still matters:

- `ENABLE_LINUX_BUILDS=true`

## 6. Expected Linux Output

Once the runner is online and `ENABLE_LINUX_BUILDS=true`, the workflow should
publish binaries here:

`publish/TVectorBlur/bin/<nuke_version>/linux/TVectorBlur.so`
