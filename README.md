# Baby's First UEFI Driver

This is an open source NTFS driver with added code to write a file to the Windows startup folder—a toy bootkit used to learn UEFI driver development concepts.

I describe what I learned in detail in this [blog post](TODO link). This README reproduces the build process section of the post.

## Build Process

I built and tested this driver in a Windows VM using an OVMF image, QEMU, and an NTFS-formatted virtual hard disk (VHD). 

OVMF is a port of Intel's tianocore firmware to the QEMU virtual machine. Here's an [informative whitepaper](https://www.linux-kvm.org/downloads/lersek/ovmf-whitepaper-c770f8c.txt).

These instructions are offered without much explanation.

### Building and Booting an OVMF Image with QEMU

Start by building the OVMF image with edk2.

Download [edk2](https://github.com/tianocore/edk2) from source and put it in C:\Workspace (or another short path with no spaces close to the root).
To make the build tools and configure the shell environment, run:
```
cd C:\Workspace\edk2\
C:\Workspace\edk2\edksetup.bat Rebuild
C:\Workspace\edk2\edksetup.bat
```
It's important to run them from the command prompt, not PowerShell. Also, don't close the prompt in between configuring the environment and building the image with:

```
build -p .\OvmfPkg\OvmfPkgX64.dsc -t VS2019 -a X64 -b DEBUG -D DEBUG_ON_SERIAL_PORT -D FD_SIZE_4MB
```
which produces a debug build of the 64-bit OVMF image using the VS2019 toolchain. `-D DEBUG_ON_SERIAL_PORT` enables debug output on the serial port (used by QEMU). `-D FD_SIZE_4MB` builds an image with 4MB of flash memory. 

You'll find the image at C:\Workspace\edk2\Build\OvmfX64\DEBUG_VS2019\FV\OVMF.fd.

See OvmfPkg\OvmfPkgX64.dsc and OvmfPkg\README for build options and instructions for running the image with QEMU.

The build output file, OVMF.fd, includes not only the executable firmware code, but the non-volatile variable store as well. For this reason, make a  VM-specific copy of the build output (the variable store should be private to the virtual machine):
```
copy Build\OvmfX64\DEBUG_VS2019\FV\OVMF.fd C:\Workspace\
```

Use:
```
qemu-system-x86_64 -drive if=pflash,format=raw,file=C:\Workspace\OVMF.fd -serial file:C:\Workspace\serial.log -nic none
```
to boot the firmware image and output debug info to serial.log. `-nic none` excludes a NIC from QEMU's virtual hardware, which makes the boot process a lot faster.

This command doesn't provide an OS image to boot after the firmware finishes its part of the boot process, so you'll see an error message "failed to load Boot0001 ..." and then the EFI shell.

serial.log now contains a fascinating if dry play-by-play of the boot process.

### Building the Driver

Clone the driver package into C:\Workspace\edk2\OvmfPkg:
```
git clone https://github.com/athabasca/NtfsPkg.git C:\Workspace\edk2\OvmfPkg
```

If you want to see a bunch of debug output in serial.log explaining what the driver is doing, make a backup of Ntfs.c and rename Ntfs-debug.c to Ntfs.c.

Add `OvmfPkg/NtfsPkg/NtfsDxe/Ntfs.inf` to OvmfPkgX64.dsc at the end of the DXE modules section  and `INF  OvmfPkg/NtfsPkg/NtfsDxe/Ntfs.inf` to OvmfPkgX64.fdf at the end of the DXE phase modules section.

Build just the driver with:
```
build -p .\OvmfPkg\OvmfPkgX64.dsc -t VS2019 -a X64 -b DEBUG -D DEBUG_ON_SERIAL_PORT -D FD_SIZE_4MB -m OvmfPkg\NtfsPkg\NtfsDxe\Ntfs.inf
```
Building the driver alone, which takes 15 seconds, lets you see any compiler errors before you spend 4 minutes building the whole image.

If it's successful, build the image and copy to to the workspace:
```
build -p .\OvmfPkg\OvmfPkgX64.dsc -t VS2019 -a X64 -b DEBUG -D DEBUG_ON_SERIAL_PORT -D FD_SIZE_4MB
copy C:\Workspace\edk2\Build\OvmfX64\DEBUG_VS2019\FV\OVMF.fd C:\Workspace\
```

### Testing the Driver

To test the driver, you need to attach an NTFS drive with some Windows folders on it to QEMU.

Create an NTFS VHD with Disk Manager. At the root, create a folder called Windows and all the folders in the path ProgramData\Microsoft\Windows\Start Menu\Programs\Startup.

Detach the VHD and save it to C:\Workspace\ntfs.vhd.

Run:
```
qemu-system-x86_64 -drive if=pflash,format=raw,file=c:\workspace\OVMF.fd -serial file:c:\workspace\serial.log -nic none -drive file=c:\workspace\ntfs.vhd,format=raw
```

If you used Ntfs-debug.c, you can check serial.log to see if the implant worked. Otherwise, attach the VHD again and check the startup folder for test.txt.