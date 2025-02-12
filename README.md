> **IMPORTANT!** This material is a work in progress.

## TEMPORARY: How to Test WAP3

 1. Follow the instructions below for setting up the [Development Environment](#development-environment) for ESP-IDF
 2. In a terminal (in the Docker container), navigate into the *http_request/* directory.

```sh
cd /workspace/apps/http_request/
```

 3. Change your target in *sdkconfig.defaults* to your intended ESP32 variant. For example:

```sh
CONFIG_IDF_TARGET="esp32s3"
```

 4. Bring up *menuconfig*

```sh
idf.py menuconfig
```

 5. Adjust the settings in **Component config → WiFi STA Configuration**
    * Enable **Connect using WiFi**
    * Set **IP version** (IPv4, IPv6, or either)
    * Change **Minimum WiFi authentication mode** to **WPA3 PSK**
    * Set your **WiFi SSID** and **WiFi password** (if using PSK)
    * Set **SAE mode** and **Password identifier for SAE** (if applicable)
 6. Press `q` and `y` to save and exit
 7. Build the project:

```sh
idf.py build
```

 8. Back on your host computer (outside the container), navigate to this directory and activate the virtual environment:

*Linux/macOS:*

```sh
source venv/bin/activate
```

*Windows (PowerShell):*

```bat
venv\Scripts\activate
```

 9. Flash the project (change `<SERIAL_PORT>` to the serial port of your ESP32, such as `COM3` for Windows, `/dev/ttyUSB0` for Linux, or `/dev/cu.usbserial-1401` for macOS):

*ESP32-S3* (bootloader at 0x0):

```sh
cd workspace/apps/http_request
python -m esptool --port "<SERIAL_PORT>" --chip auto --baud 921600 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/app.bin
```

*Other ESP32 variants* (bootloader at 0x1000):

```sh
cd workspace/apps/http_request
python -m esptool --port "<SERIAL_PORT>" --chip auto --baud 921600 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/app.bin
```

10. Monitor with a serial connection (change `<SERIAL_PORT>` to the serial port of your ESP32):

```sh
python -m serial.tools.miniterm "<SERIAL_PORT>" 115200
```

11. You should see the contents of [example.com](https://example.com/index.html) printed to the console after a few moments.
12. If you see any errors (especially as it relates to WPA3), please let me know in the **issues** or submit a **pull request** if you know how to fix it!


## Development Environment

This is a development environment for experimenting with MCUboot for various embedded development frameworks. The idea is to run the development environment (and QEMU runtime) in a Docker container, and you connect to that container using a browser or local VS Code. You can emulate your code with QEMU or flash a real board using your host system.

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

*Linux/macOS:*

```sh
python -m venv venv
source venv/bin/activate
python -m pip install pyserial==3.5 esptool==4.8.1
```

*Windows (PowerShell):*

```bat
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy Unrestricted -Force
python -m venv venv
venv\Scripts\activate
python -m pip install pyserial==3.5 esptool==4.8.1
```

From this directory, build the image (this will take some time):

```sh
docker build -t env-esp-idf -f Dockerfile.esp-idf .
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

If you want to develop Zephyr applications using your local instance of VS Code, you can connect to the running container using SSH. This will allow you to use your custom themes, extensions, settings, etc.

In your local VS Code, install the [Remote - SSH extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-ssh).

Open the extension in VS Code and create a new connection: **root@localhost:22001**.

Connect and login using the password in the Dockerfile (default: `fota`). Go to **File > Open Workspace from File..** and select the **/esp-idf.code-workspace** file when prompted. Enter the password again if requested. This should configure your VS Code workspace with the */workspace* directory mapped from the host directory alongside the required toolchain directories (e.g. */opt/toolchains/esp-idf*).

### Recommended Extensions

I recommend installing the following VS Code extensions to make working with Zephyr easier (e.g. IntelliSense). Note that the *.code-workspace* file will automatically recommend them.

 * [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)AZ
 * [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
 * [Microsoft Hex Editor](https://marketplace.visualstudio.com/items?itemName=ms-vscode.hexeditor)