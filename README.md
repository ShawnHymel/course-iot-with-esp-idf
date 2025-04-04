> **IMPORTANT!** This material is a work in progress.

## Development Environment

This is a development environment for the *IoT Firmware Development with ESP32 Using ESP-IDF* course. The idea is to run the development environment (and QEMU runtime) in a Docker container, and you connect to that container using a browser or local VS Code. You can emulate your code with QEMU or flash a real board using your host system.

> **Note**: the instructions below were verified with Python 3.12 running on the host system. If one of the *pip install* steps fails, try installing exactly Python 3.12 and running the command again with `python3.12 -m pip install ...`

You have a few options for using this development environment:

 1. (Default) The container runs *code-server* so that you can connect to `localhost:8800` via a browser to get a pre-made VS Code instance
 2. Run the image. In your local VS Code, install the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) or [Remote - SSH extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-ssh). Connect to the running container. Select *File > Open Workspace from File...* and select the */zephyr.code-workspace* file when prompted.
 3. Edit files locally (e.g. with VS Code) and log into the container via SSH to build and run the emulator.
    * Username: `root`
    * Password: `espidf`

## Getting Started

Before you start, install the following programs on your computer:

 * (Windows) [WSL 2](https://learn.microsoft.com/en-us/windows/wsl/install)
 * [Docker Desktop](https://www.docker.com/products/docker-desktop/)
 * [Python](https://www.python.org/downloads/)

Windows users will likely need to install the [virtual COM port (VCP) drivers from SiLabs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads).

### Install Dependencies

Open a terminal, navigate to this directory, and install the following dependencies:

Linux/macOS:

```sh
python -m venv venv
source venv/bin/activate
python -m pip install pyserial==3.5 esptool==4.8.1
```

Windows (PowerShell):

```bat
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy Unrestricted -Force
python -m venv venv
venv\Scripts\activate
python -m pip install pyserial==3.5 esptool==4.8.1
```

From this directory, build the image (this will take some time). Note that we pass in the computer's hostname so it can be added as a Subject Alternative Name (SAN) in the OpenSSL configuration. This allows TLS connections to use mDNS to connect to the host and verify the Common Name (or SAN) using `<MY_HOSTNAME>.local` in addition to `localhost`.

```sh
docker build -t env-esp-idf -f Dockerfile.esp-idf --build-arg HOSTNAME=$(hostname) .
```

You can ignore the warning about setting the password as an `ARG` in the Dockerfile. The container is fairly unsecure anyway; I only recommend running it locally when you need it. You will need to change the password and configure *code-server* and *sshd* to be more secure if you want to use it remotely.

Run the image in *VS Code Server* mode. Note that it mounts the local *workspace/* directory into the container! We also expose ports 3333 (OpenOCD), 2223 (mapped from 22 within the container for SSH), and 8800 (*code-server*).

Linux/macOS:

```sh
docker run --rm -it -p 3333:3333 -p 22001:22 -p 8800:8800 -v "$(pwd)"/workspace:/workspace -w /workspace env-esp-idf
```

Windows (PowerShell):

```bat
docker run --rm -it -p 3333:3333 -p 22001:22 -p 8800:8800 -v "${PWD}\workspace:/workspace" -w /workspace env-esp-idf
```

> **IMPORTANT**: The *entrypoint.sh* script will copy *c_cpp_properties.json* to your *workspace/.vscode* directory every time you run the image. This file helps *IntelliSense* know where to find things. Don't mess with this file!

Alternatively, you can run the image in interactive mode by adding the `--entrypoint /bin/bash` argument. This will allow you to skip running the VS Code server in the background.

### Connect to Container

With the Docker image built, you have a few options to connect to the development environment: browser, Dev Containers, SSH. Choose one of the options below.

#### Option 1: Connect via Browser

Open a browser and navigate to http://localhost:8800/.

#### Option 2: VS Code Dev Containers

Dev Containers is a wonderful extension for letting you connect your local VS Code to a Docker container. Feel free to read the [official documentation](https://code.visualstudio.com/docs/devcontainers/containers) to learn more.

In your local VS Code, install the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension.

Open the command palette (Ctrl+Shift+P) and search for **Dev Containers: Attach to Running Container**. Click it, and you should see a container of your *env-esp-idf* image running. Click the container from the list. A new VS Code window will open and install the required dependencies.

Go to **File > Open Workspace from File..** and select the **/esp-idf.code-workspace** file when prompted. Enter the password again if requested. This should configure your VS Code workspace with the */workspace* directory mapped from the host directory alongside the required toolchain directories (e.g. */opt/toolchains/esp-idf*).

#### Option 3: VS Code SSH

If you want to develop ESP-IDF applications using your local instance of VS Code, you can connect to the running container using SSH. This will allow you to use your custom themes, extensions, settings, etc.

In your local VS Code, install the [Remote - SSH extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-ssh).

Open the extension in VS Code and create a new connection: **root@localhost:22001**.

Connect and login using the password in the Dockerfile (default: `fota`). Go to **File > Open Workspace from File..** and select the **/esp-idf.code-workspace** file when prompted. Enter the password again if requested. This should configure your VS Code workspace with the */workspace* directory mapped from the host directory alongside the required toolchain directories (e.g. */opt/toolchains/esp-idf*).

### Recommended Extensions

I recommend installing the following VS Code extensions to make working with Zephyr easier (e.g. IntelliSense). Note that the *.code-workspace* file will automatically recommend them.

 * [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)AZ
 * [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
 * [Microsoft Hex Editor](https://marketplace.visualstudio.com/items?itemName=ms-vscode.hexeditor)

## Regenerate Certificate Authority (CA) Key and Certificate

> **WARNING!** Only do this if you know what you are doing. This can break your applications in the ESP-IDF container if the *ca.crt* used to sign the Mosquitto server's certificate and the *ca.crt* uploaded to the ESP32 flash do not match.

Generate new CA key and certificate, and copy them into the *scripts/esp-idf* directory.

Linux/macOS:

```sh
docker build -t ca-gen -f Dockerfile.ca-gen .
docker run --rm -v "$(pwd)/scripts/esp-idf:/output" ca-gen
docker image rm ca-gen
```

Windows (PowerShell):

```
docker build -t ca-gen -f Dockerfile.ca-gen .
docker run --rm -v "${PWD}/scripts/esp-idf:/output" ca-gen
docker image rm ca-gen
```

You can now rebuild the ESP-IDF image, and the newly generated CA key and certificate will be copied into the image.

> **Note:** Yes, I know *ca.key* is not secure by sharing it. It is for *educational purposes only*. For creating an actual server with self-signed certificates, you will want to generate *ca.key* and *ca.crt*; do NOT share *ca.key*!

## License

All software in this repository, unless otherwise noted, is licensed under the [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0) license.

``` 
Copyright 2025 Shawn Hymel

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
