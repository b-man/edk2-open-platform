/** @file
  This driver is used to manage SD/MMC PCI host controllers which are compliance
  with SD Host Controller Simplified Specification version 3.00.

  It would expose EFI_SD_MMC_PASS_THRU_PROTOCOL for upper layer use.

  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.<BR>
  Copyright (c) 2016, Marvell. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "XenonSdhci.h"

//
// Driver Global Variables
//
EFI_DRIVER_BINDING_PROTOCOL gSdMmcPciHcDriverBinding = {
  SdMmcPciHcDriverBindingSupported,
  SdMmcPciHcDriverBindingStart,
  SdMmcPciHcDriverBindingStop,
  0x10,
  NULL,
  NULL
};

//
// Template for SD/MMC host controller private data.
//
SD_MMC_HC_PRIVATE_DATA gSdMmcPciHcTemplate = {
  SD_MMC_HC_PRIVATE_SIGNATURE,      // Signature
  NULL,                             // ControllerHandle
  NULL,                             // PciIo
  {                                 // PassThru
    sizeof (UINT32),
    SdMmcPassThruPassThru,
    SdMmcPassThruGetNextSlot,
    SdMmcPassThruBuildDevicePath,
    SdMmcPassThruGetSlotNumber,
    SdMmcPassThruResetDevice
  },
  0,                                // PciAttributes
  0,                                // PreviousSlot
  NULL,                             // TimerEvent
  NULL,                             // ConnectEvent
                                    // Queue
  INITIALIZE_LIST_HEAD_VARIABLE (gSdMmcPciHcTemplate.Queue),
  {                                 // Slot
    {0, UnknownSlot, 0, 0, 0}, {0, UnknownSlot, 0, 0, 0}, {0, UnknownSlot, 0, 0, 0},
    {0, UnknownSlot, 0, 0, 0}, {0, UnknownSlot, 0, 0, 0}, {0, UnknownSlot, 0, 0, 0}
  },
  {                                 // Capability
    {0},
  },
  {                                 // MaxCurrent
    0,
  },
  0                                 // ControllerVersion
};

SD_DEVICE_PATH    mSdDpTemplate = {
  {
    MESSAGING_DEVICE_PATH,
    MSG_SD_DP,
    {
      (UINT8) (sizeof (SD_DEVICE_PATH)),
      (UINT8) ((sizeof (SD_DEVICE_PATH)) >> 8)
    }
  },
  0
};

EMMC_DEVICE_PATH    mEmmcDpTemplate = {
  {
    MESSAGING_DEVICE_PATH,
    MSG_EMMC_DP,
    {
      (UINT8) (sizeof (EMMC_DEVICE_PATH)),
      (UINT8) ((sizeof (EMMC_DEVICE_PATH)) >> 8)
    }
  },
  0
};

//
// Prioritized function list to detect card type.
// User could add other card detection logic here.
//
CARD_TYPE_DETECT_ROUTINE mCardTypeDetectRoutineTable[] = {
  EmmcIdentification,
  SdCardIdentification,
  NULL
};

/**
  The entry point for SD host controller driver, used to install this driver on the ImageHandle.

  @param[in]  ImageHandle   The firmware allocated handle for this driver image.
  @param[in]  SystemTable   Pointer to the EFI system table.

  @retval EFI_SUCCESS   Driver loaded.
  @retval other         Driver not loaded.

**/
EFI_STATUS
EFIAPI
InitializeSdMmcPciHcDxe (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gSdMmcPciHcDriverBinding,
             ImageHandle,
             &gSdMmcPciHcComponentName,
             &gSdMmcPciHcComponentName2
             );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

/**
  Call back function when the timer event is signaled.

  @param[in]  Event     The Event this notify function registered to.
  @param[in]  Context   Pointer to the context data registered to the
                        Event.

**/
VOID
EFIAPI
ProcessAsyncTaskList (
  IN EFI_EVENT          Event,
  IN VOID*              Context
  )
{
  SD_MMC_HC_PRIVATE_DATA              *Private;
  LIST_ENTRY                          *Link;
  SD_MMC_HC_TRB                       *Trb;
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  BOOLEAN                             InfiniteWait;
  EFI_EVENT                           TrbEvent;

  Private = (SD_MMC_HC_PRIVATE_DATA*)Context;

  //
  // Check if the first entry in the async I/O queue is done or not.
  //
  Status = EFI_SUCCESS;
  Trb    = NULL;
  Link   = GetFirstNode (&Private->Queue);
  if (!IsNull (&Private->Queue, Link)) {
    Trb = SD_MMC_HC_TRB_FROM_THIS (Link);
    if (!Private->Slot[Trb->Slot].MediaPresent) {
      Status = EFI_NO_MEDIA;
      goto Done;
    }
    if (!Trb->Started) {
      //
      // Check whether the cmd/data line is ready for transfer.
      //
      Status = SdMmcCheckTrbEnv (Private, Trb);
      if (!EFI_ERROR (Status)) {
        Trb->Started = TRUE;
        Status = SdMmcExecTrb (Private, Trb);
        if (EFI_ERROR (Status)) {
          goto Done;
        }
      } else {
        goto Done;
      }
    }
    Status = SdMmcCheckTrbResult (Private, Trb);
  }

Done:
  if ((Trb != NULL) && (Status == EFI_NOT_READY)) {
    Packet = Trb->Packet;
    if (Packet->Timeout == 0) {
      InfiniteWait = TRUE;
    } else {
      InfiniteWait = FALSE;
    }
    if ((!InfiniteWait) && (Trb->Timeout-- == 0)) {
      RemoveEntryList (Link);
      Trb->Packet->TransactionStatus = EFI_TIMEOUT;
      TrbEvent = Trb->Event;
      SdMmcFreeTrb (Trb);
      DEBUG ((DEBUG_VERBOSE, "ProcessAsyncTaskList(): Signal Event %p EFI_TIMEOUT\n", TrbEvent));
      gBS->SignalEvent (TrbEvent);
      return;
    }
  }
  if ((Trb != NULL) && (Status != EFI_NOT_READY)) {
    RemoveEntryList (Link);
    Trb->Packet->TransactionStatus = Status;
    TrbEvent = Trb->Event;
    SdMmcFreeTrb (Trb);
    DEBUG ((DEBUG_VERBOSE, "ProcessAsyncTaskList(): Signal Event %p with %r\n", TrbEvent, Status));
    gBS->SignalEvent (TrbEvent);
  }
  return;
}

/**
  Sd removable device enumeration callback function when the timer event is signaled.

  @param[in]  Event     The Event this notify function registered to.
  @param[in]  Context   Pointer to the context data registered to the
                        Event.

**/
VOID
EFIAPI
SdMmcPciHcEnumerateDevice (
  IN EFI_EVENT          Event,
  IN VOID*              Context
  )
{
  SD_MMC_HC_PRIVATE_DATA              *Private;
  EFI_STATUS                          Status;
  UINT8                               Slot;
  BOOLEAN                             MediaPresent;
  UINT32                              RoutineNum;
  CARD_TYPE_DETECT_ROUTINE            *Routine;
  UINTN                               Index;
  LIST_ENTRY                          *Link;
  LIST_ENTRY                          *NextLink;
  SD_MMC_HC_TRB                       *Trb;
  EFI_TPL                             OldTpl;

  Private = (SD_MMC_HC_PRIVATE_DATA*)Context;

  for (Slot = 0; Slot < SD_MMC_HC_MAX_SLOT; Slot++) {
    if ((Private->Slot[Slot].Enable) && (Private->Slot[Slot].SlotType == RemovableSlot)) {
      Status = SdMmcHcCardDetect (Private->PciIo, Slot, &MediaPresent);
      if ((Status == EFI_MEDIA_CHANGED) && !MediaPresent) {
        DEBUG ((DEBUG_INFO, "SdMmcPciHcEnumerateDevice: device disconnected at slot %d of pci %p\n", Slot, Private->PciIo));
        Private->Slot[Slot].MediaPresent = FALSE;
        Private->Slot[Slot].Initialized  = FALSE;
        //
        // Signal all async task events at the slot with EFI_NO_MEDIA status.
        //
        OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
        for (Link = GetFirstNode (&Private->Queue);
             !IsNull (&Private->Queue, Link);
             Link = NextLink) {
          NextLink = GetNextNode (&Private->Queue, Link);
          Trb = SD_MMC_HC_TRB_FROM_THIS (Link);
          if (Trb->Slot == Slot) {
            RemoveEntryList (Link);
            Trb->Packet->TransactionStatus = EFI_NO_MEDIA;
            gBS->SignalEvent (Trb->Event);
            SdMmcFreeTrb (Trb);
          }
        }
        gBS->RestoreTPL (OldTpl);
        //
        // Notify the upper layer the connect state change through ReinstallProtocolInterface.
        //
        gBS->ReinstallProtocolInterface (
              Private->ControllerHandle,
              &gEfiSdMmcPassThruProtocolGuid,
              &Private->PassThru,
              &Private->PassThru
              );
      }
      if ((Status == EFI_MEDIA_CHANGED) && MediaPresent) {
        DEBUG ((DEBUG_INFO, "SdMmcPciHcEnumerateDevice: device connected at slot %d of pci %p\n", Slot, Private->PciIo));
        //
        // Reset the specified slot of the SD/MMC Pci Host Controller
        //
        Status = SdMmcHcReset (Private->PciIo, Slot);
        if (EFI_ERROR (Status)) {
          continue;
        }
        //
        // Reinitialize slot and restart identification process for the new attached device
        //
        Status = SdMmcHcInitHost (Private->PciIo, Slot, Private->Capability[Slot]);
        if (EFI_ERROR (Status)) {
          continue;
        }

        Private->Slot[Slot].MediaPresent = TRUE;
        Private->Slot[Slot].Initialized  = TRUE;
        RoutineNum = sizeof (mCardTypeDetectRoutineTable) / sizeof (CARD_TYPE_DETECT_ROUTINE);
        for (Index = 0; Index < RoutineNum; Index++) {
          Routine = &mCardTypeDetectRoutineTable[Index];
          if (*Routine != NULL) {
            Status = (*Routine) (Private, Slot);
            if (!EFI_ERROR (Status)) {
              break;
            }
          }
        }
        //
        // This card doesn't get initialized correctly.
        //
        if (Index == RoutineNum) {
          Private->Slot[Slot].Initialized = FALSE;
        }

        //
        // Notify the upper layer the connect state change through ReinstallProtocolInterface.
        //
        gBS->ReinstallProtocolInterface (
               Private->ControllerHandle,
               &gEfiSdMmcPassThruProtocolGuid,
               &Private->PassThru,
               &Private->PassThru
               );
      }
    }
  }

  return;
}
/**
  Tests to see if this driver supports a given controller. If a child device is provided,
  it further tests to see if this driver supports creating a handle for the specified child device.

  This function checks to see if the driver specified by This supports the device specified by
  ControllerHandle. Drivers will typically use the device path attached to
  ControllerHandle and/or the services from the bus I/O abstraction attached to
  ControllerHandle to determine if the driver supports ControllerHandle. This function
  may be called many times during platform initialization. In order to reduce boot times, the tests
  performed by this function must be very small, and take as little time as possible to execute. This
  function must not change the state of any hardware devices, and this function must be aware that the
  device specified by ControllerHandle may already be managed by the same driver or a
  different driver. This function must match its calls to AllocatePages() with FreePages(),
  AllocatePool() with FreePool(), and OpenProtocol() with CloseProtocol().
  Since ControllerHandle may have been previously started by the same driver, if a protocol is
  already in the opened state, then it must not be closed with CloseProtocol(). This is required
  to guarantee the state of ControllerHandle is not modified by this function.

  @param[in]  This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL instance.
  @param[in]  ControllerHandle     The handle of the controller to test. This handle
                                   must support a protocol interface that supplies
                                   an I/O abstraction to the driver.
  @param[in]  RemainingDevicePath  A pointer to the remaining portion of a device path.  This
                                   parameter is ignored by device drivers, and is optional for bus
                                   drivers. For bus drivers, if this parameter is not NULL, then
                                   the bus driver must determine if the bus controller specified
                                   by ControllerHandle and the child controller specified
                                   by RemainingDevicePath are both supported by this
                                   bus driver.

  @retval EFI_SUCCESS              The device specified by ControllerHandle and
                                   RemainingDevicePath is supported by the driver specified by This.
  @retval EFI_ALREADY_STARTED      The device specified by ControllerHandle and
                                   RemainingDevicePath is already being managed by the driver
                                   specified by This.
  @retval EFI_ACCESS_DENIED        The device specified by ControllerHandle and
                                   RemainingDevicePath is already being managed by a different
                                   driver or an application that requires exclusive access.
                                   Currently not implemented.
  @retval EFI_UNSUPPORTED          The device specified by ControllerHandle and
                                   RemainingDevicePath is not supported by the driver specified by This.
**/
EFI_STATUS
EFIAPI
SdMmcPciHcDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  Controller,
  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *ParentDevicePath;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  PCI_TYPE00                PciData;

  PciIo            = NULL;
  ParentDevicePath = NULL;

  //
  // SdPciHcDxe is a device driver, and should ingore the
  // "RemainingDevicePath" according to EFI spec.
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID *) &ParentDevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    //
    // EFI_ALREADY_STARTED is also an error.
    //
    return Status;
  }
  //
  // Close the protocol because we don't use it here.
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiDevicePathProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  //
  // Now test the EfiPciIoProtocol.
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Now further check the PCI header: Base class (offset 0x08) and
  // Sub Class (offset 0x05). This controller should be an SD/MMC PCI
  // Host Controller.
  //
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (PciData),
                        &PciData
                        );
  if (EFI_ERROR (Status)) {
    gBS->CloseProtocol (
          Controller,
          &gEfiPciIoProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
    return EFI_UNSUPPORTED;
  }
  //
  // Since we already got the PciData, we can close protocol to avoid to carry it
  // on for multiple exit points.
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  //
  // Examine SD PCI Host Controller PCI Configuration table fields.
  //
  if ((PciData.Hdr.ClassCode[2] == PCI_CLASS_SYSTEM_PERIPHERAL) &&
      (PciData.Hdr.ClassCode[1] == PCI_SUBCLASS_SD_HOST_CONTROLLER) &&
      ((PciData.Hdr.ClassCode[0] == 0x00) || (PciData.Hdr.ClassCode[0] == 0x01))) {
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

/**
  Starts a device controller or a bus controller.

  The Start() function is designed to be invoked from the EFI boot service ConnectController().
  As a result, much of the error checking on the parameters to Start() has been moved into this
  common boot service. It is legal to call Start() from other locations,
  but the following calling restrictions must be followed or the system behavior will not be deterministic.
  1. ControllerHandle must be a valid EFI_HANDLE.
  2. If RemainingDevicePath is not NULL, then it must be a pointer to a naturally aligned
     EFI_DEVICE_PATH_PROTOCOL.
  3. Prior to calling Start(), the Supported() function for the driver specified by This must
     have been called with the same calling parameters, and Supported() must have returned EFI_SUCCESS.

  @param[in]  This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL instance.
  @param[in]  ControllerHandle     The handle of the controller to start. This handle
                                   must support a protocol interface that supplies
                                   an I/O abstraction to the driver.
  @param[in]  RemainingDevicePath  A pointer to the remaining portion of a device path.  This
                                   parameter is ignored by device drivers, and is optional for bus
                                   drivers. For a bus driver, if this parameter is NULL, then handles
                                   for all the children of Controller are created by this driver.
                                   If this parameter is not NULL and the first Device Path Node is
                                   not the End of Device Path Node, then only the handle for the
                                   child device specified by the first Device Path Node of
                                   RemainingDevicePath is created by this driver.
                                   If the first Device Path Node of RemainingDevicePath is
                                   the End of Device Path Node, no child handle is created by this
                                   driver.

  @retval EFI_SUCCESS              The device was started.
  @retval EFI_DEVICE_ERROR         The device could not be started due to a device error.Currently not implemented.
  @retval EFI_OUT_OF_RESOURCES     The request could not be completed due to a lack of resources.
  @retval Others                   The driver failded to start the device.

**/
EFI_STATUS
EFIAPI
SdMmcPciHcDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN EFI_HANDLE                      Controller,
  IN EFI_DEVICE_PATH_PROTOCOL        *RemainingDevicePath
  )
{
  EFI_STATUS                      Status;
  SD_MMC_HC_PRIVATE_DATA          *Private;
  EFI_PCI_IO_PROTOCOL             *PciIo;
  UINT64                          Supports;
  UINT64                          PciAttributes;
  UINT8                           Slot;
  UINT8                           Index;
  CARD_TYPE_DETECT_ROUTINE        *Routine;
  UINT32                          RoutineNum;
  BOOLEAN                         Support64BitDma;

  DEBUG ((DEBUG_INFO, "SdMmcPciHcDriverBindingStart: Start\n"));

  //
  // Open PCI I/O Protocol and save pointer to open protocol
  // in private data area.
  //
  PciIo  = NULL;
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Enable the SD Host Controller MMIO space
  //
  Private = NULL;
  Status  = PciIo->Attributes (
                     PciIo,
                     EfiPciIoAttributeOperationGet,
                     0,
                     &PciAttributes
                     );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationSupported,
                    0,
                    &Supports
                    );

  if (!EFI_ERROR (Status)) {
    Supports &= (UINT64)EFI_PCI_DEVICE_ENABLE;
    Status    = PciIo->Attributes (
                         PciIo,
                         EfiPciIoAttributeOperationEnable,
                         Supports,
                         NULL
                         );
  } else {
    goto Done;
  }

  Private = AllocateCopyPool (sizeof (SD_MMC_HC_PRIVATE_DATA), &gSdMmcPciHcTemplate);
  if (Private == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Private->ControllerHandle = Controller;
  Private->PciIo            = PciIo;
  Private->PciAttributes    = PciAttributes;
  InitializeListHead (&Private->Queue);

  Support64BitDma = TRUE;

  // There is only one slot 0 on Xenon
  Slot = 0;
  Private->Slot[Slot].Enable = TRUE;

  Status = SdMmcHcGetCapability (PciIo, Slot, &Private->Capability[Slot]);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Support64BitDma &= Private->Capability[Slot].SysBus64;

  //
  // Override capabilities structure - only 4 Bit width bus is supported
  // by HW and also force using SDR25 mode
  //
  Private->Capability[Slot].Sdr104 = 0;
  Private->Capability[Slot].Ddr50 = 0;
  Private->Capability[Slot].Sdr50 = 0;
  Private->Capability[Slot].BusWidth8 = 0;

  if (Private->Capability[Slot].BaseClkFreq == 0) {
    Private->Capability[Slot].BaseClkFreq = 0xff;
  }

  DumpCapabilityReg (Slot, &Private->Capability[Slot]);

  Status = SdMmcHcGetMaxCurrent (PciIo, Slot, &Private->MaxCurrent[Slot]);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Private->Slot[Slot].SlotType = Private->Capability[Slot].SlotType;
  if ((Private->Slot[Slot].SlotType != RemovableSlot) && (Private->Slot[Slot].SlotType != EmbeddedSlot)) {
    DEBUG ((DEBUG_INFO, "SdMmcPciHcDxe doesn't support the slot type [%d]!!!\n", Private->Slot[Slot].SlotType));
    return EFI_D_ERROR;
  }

  // Perform Xenon-specific init sequence
  XenonInit (Private);

  Private->Slot[Slot].MediaPresent = TRUE;
  Private->Slot[Slot].Initialized  = TRUE;
  RoutineNum = sizeof (mCardTypeDetectRoutineTable) / sizeof (CARD_TYPE_DETECT_ROUTINE);
  for (Index = 0; Index < RoutineNum; Index++) {
    Routine = &mCardTypeDetectRoutineTable[Index];
    if (*Routine != NULL) {
      Status = (*Routine) (Private, Slot);
      if (!EFI_ERROR (Status)) {
        break;
      }
    }
  }
  //
  // This card doesn't get initialized correctly.
  //
  if (Index == RoutineNum) {
    Private->Slot[Slot].Initialized = FALSE;
  }

  //
  // Enable 64-bit DMA support in the PCI layer if this controller
  // supports it.
  //
  if (Support64BitDma) {
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE,
                      NULL
                      );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "SdMmcPciHcDriverBindingStart: failed to enable 64-bit DMA (%r)\n", Status));
    }
  }

  //
  // Start the asynchronous I/O monitor
  //
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ProcessAsyncTaskList,
                  Private,
                  &Private->TimerEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = gBS->SetTimer (Private->TimerEvent, TimerPeriodic, SD_MMC_HC_ASYNC_TIMER);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Start the Sd removable device connection enumeration
  //
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  SdMmcPciHcEnumerateDevice,
                  Private,
                  &Private->ConnectEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = gBS->SetTimer (Private->ConnectEvent, TimerPeriodic, SD_MMC_HC_ENUM_TIMER);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiSdMmcPassThruProtocolGuid,
                  &(Private->PassThru),
                  NULL
                  );

  DEBUG ((DEBUG_INFO, "SdMmcPciHcDriverBindingStart: %r End on %x\n", Status, Controller));

Done:
  if (EFI_ERROR (Status)) {
    if ((Private != NULL) && (Private->PciAttributes != 0)) {
      //
      // Restore original PCI attributes
      //
      PciIo->Attributes (
               PciIo,
               EfiPciIoAttributeOperationSet,
               Private->PciAttributes,
               NULL
               );
    }
    gBS->CloseProtocol (
          Controller,
          &gEfiPciIoProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );

    if ((Private != NULL) && (Private->TimerEvent != NULL)) {
      gBS->CloseEvent (Private->TimerEvent);
    }

    if ((Private != NULL) && (Private->ConnectEvent != NULL)) {
      gBS->CloseEvent (Private->ConnectEvent);
    }

    if (Private != NULL) {
      FreePool (Private);
    }
  }

  return Status;
}

/**
  Stops a device controller or a bus controller.

  The Stop() function is designed to be invoked from the EFI boot service DisconnectController().
  As a result, much of the error checking on the parameters to Stop() has been moved
  into this common boot service. It is legal to call Stop() from other locations,
  but the following calling restrictions must be followed or the system behavior will not be deterministic.
  1. ControllerHandle must be a valid EFI_HANDLE that was used on a previous call to this
     same driver's Start() function.
  2. The first NumberOfChildren handles of ChildHandleBuffer must all be a valid
     EFI_HANDLE. In addition, all of these handles must have been created in this driver's
     Start() function, and the Start() function must have called OpenProtocol() on
     ControllerHandle with an Attribute of EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER.

  @param[in]  This              A pointer to the EFI_DRIVER_BINDING_PROTOCOL instance.
  @param[in]  ControllerHandle  A handle to the device being stopped. The handle must
                                support a bus specific I/O protocol for the driver
                                to use to stop the device.
  @param[in]  NumberOfChildren  The number of child device handles in ChildHandleBuffer.
  @param[in]  ChildHandleBuffer An array of child handles to be freed. May be NULL
                                if NumberOfChildren is 0.

  @retval EFI_SUCCESS           The device was stopped.
  @retval EFI_DEVICE_ERROR      The device could not be stopped due to a device error.

**/
EFI_STATUS
EFIAPI
SdMmcPciHcDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Controller,
  IN  UINTN                           NumberOfChildren,
  IN  EFI_HANDLE                      *ChildHandleBuffer
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_PROTOCOL       *PassThru;
  SD_MMC_HC_PRIVATE_DATA              *Private;
  EFI_PCI_IO_PROTOCOL                 *PciIo;
  LIST_ENTRY                          *Link;
  LIST_ENTRY                          *NextLink;
  SD_MMC_HC_TRB                       *Trb;

  DEBUG ((DEBUG_INFO, "SdMmcPciHcDriverBindingStop: Start\n"));

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiSdMmcPassThruProtocolGuid,
                  (VOID**) &PassThru,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (PassThru);
  //
  // Close Non-Blocking timer and free Task list.
  //
  if (Private->TimerEvent != NULL) {
    gBS->CloseEvent (Private->TimerEvent);
    Private->TimerEvent = NULL;
  }
  if (Private->ConnectEvent != NULL) {
    gBS->CloseEvent (Private->ConnectEvent);
    Private->ConnectEvent = NULL;
  }
  //
  // As the timer is closed, there is no needs to use TPL lock to
  // protect the critical region "queue".
  //
  for (Link = GetFirstNode (&Private->Queue);
       !IsNull (&Private->Queue, Link);
       Link = NextLink) {
    NextLink = GetNextNode (&Private->Queue, Link);
    RemoveEntryList (Link);
    Trb = SD_MMC_HC_TRB_FROM_THIS (Link);
    Trb->Packet->TransactionStatus = EFI_ABORTED;
    gBS->SignalEvent (Trb->Event);
    SdMmcFreeTrb (Trb);
  }

  //
  // Uninstall Block I/O protocol from the device handle
  //
  Status = gBS->UninstallProtocolInterface (
                  Controller,
                  &gEfiSdMmcPassThruProtocolGuid,
                  &(Private->PassThru)
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         Controller,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );
  //
  // Restore original PCI attributes
  //
  PciIo  = Private->PciIo;
  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationSet,
                    Private->PciAttributes,
                    NULL
                    );
  ASSERT_EFI_ERROR (Status);

  FreePool (Private);

  DEBUG ((DEBUG_INFO, "SdMmcPciHcDriverBindingStop: End with %r\n", Status));

  return Status;
}

/**
  Check if card is ready for next data transfer by reading its status.

  @param[in]  This              A pointer to the EFI_SD_MMC_PASS_THRU_PROTOCOL instance.
  @param[in]  Slot              The slot number of the device to send the command to.
  @param[in]  Rca               The relative device address of addressed device.
  @param[in]  Timeout           The timeout in miliseconds.

  @retval EFI_SUCCESS           Card is ready for next data transfer.
  @retval EFI_DEVICE_ERROR      Card status is erroneous.
  @retval EFI_TIMEOUT           Card is busy.

**/
STATIC
EFI_STATUS
SdMmcIsReady (
  IN EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  IN UINT8                         Slot,
  IN UINT16                        Rca,
  IN UINTN                         Timeout
  )
{
  EFI_STATUS Status;
  UINT32 DevStatus;

  do {
    Status = SdCardSendStatus (This, Slot, Rca, &DevStatus);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Cannot read SD status\n"));
      return Status;
    }
    // Check device status
    if ((DevStatus & READY_FOR_DATA) && (DevStatus & CURRENT_STATE_MASK) != CURRENT_STATE_N_READY_MASK) {
      break;
    } else if (DevStatus & CARD_STATUS_ERROR_MASK) {
      DEBUG ((DEBUG_ERROR, "SD Status error\n"));
      return EFI_DEVICE_ERROR;
    }

    //
    // Wait for buffer empty signalling on the bus, what
    // indicates that device is ready for next data transfer.
    // Read CARD_STATUS every 1ms to give device enough time
    // to update its value.
    //
    gBS->Stall (1000);
  } while (Timeout--);

  if (Timeout <= 0) {
    DEBUG ((DEBUG_ERROR, "SD Status timeout\n"));
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CheckParameters (
  IN EFI_SD_MMC_PASS_THRU_PROTOCOL         *This,
  IN EFI_SD_MMC_PASS_THRU_COMMAND_PACKET   *Packet,
  IN UINT8                                 Slot
  )
{
  SD_MMC_HC_PRIVATE_DATA *Private;

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  if ((This == NULL) || (Packet == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->SdMmcCmdBlk == NULL) || (Packet->SdMmcStatusBlk == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->OutDataBuffer == NULL) && (Packet->OutTransferLength != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->InDataBuffer == NULL) && (Packet->InTransferLength != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->Slot[Slot].Enable) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->Slot[Slot].MediaPresent) {
    return EFI_NO_MEDIA;
  }

  // Currently we don't support SDIO
  if (Packet->SdMmcCmdBlk->CommandIndex == SDIO_SEND_OP_COND) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Send command SD_STOP_TRANSMISSION to stop multiple block transfer.

  @param[in]  This              A pointer to the EFI_SD_MMC_PASS_THRU_PROTOCOL instance.
  @param[in]  Slot              The slot number of the device to send the command to.

  @retval EFI_SUCCESS           The request is executed successfully.
  @retval Others                The request could not be executed successfully.

**/
STATIC
EFI_STATUS
SdMmcSendStopTransmission (
  IN     EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  IN     UINT8                         Slot
  )
{
  EFI_STATUS                           Status;
  EFI_SD_MMC_COMMAND_BLOCK             SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK              SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET  Packet;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;
  Packet.Timeout        = SD_GENERIC_TIMEOUT;

  SdMmcCmdBlk.CommandIndex = SD_STOP_TRANSMISSION;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeAc;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1b;
  SdMmcCmdBlk.CommandArgument = 0;

  Status = This->PassThru (This, Slot, &Packet, NULL);

  return Status;
}

STATIC
EFI_STATUS
WaitForInhibit (
  IN SD_MMC_HC_PRIVATE_DATA              *Private,
  IN EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet,
  IN UINT8                               Slot,
  IN BOOLEAN                             DataTransfer
  )
{
  UINT32 Mask, PresentState, Time = 0;
  UINT32 CmdTimeout = XENON_MMC_CMD_DEFAULT_TIMEOUT;

  if (Packet->SdMmcCmdBlk != NULL) {
    Mask = SDHC_CMD_INHIBIT;
  }

  if (DataTransfer != FALSE) {
    Mask = SDHC_DATA_INHIBIT;
  }

  //
  // We shouldn't wait for data inhibit for stop commands, even
  // though they might use busy signaling
  //
  if (Packet->SdMmcCmdBlk->CommandIndex == SD_STOP_TRANSMISSION)
    Mask &= ~SDHC_DATA_INHIBIT;

  SdMmcHcRwMmio (
          Private->PciIo,
          Slot,
          SD_MMC_HC_PRESENT_STATE,
          TRUE,
          sizeof (PresentState),
          &PresentState
        );

  while (PresentState & Mask) {
    if (Time >= CmdTimeout) {
      if (2 * CmdTimeout <= XENON_MMC_CMD_MAX_TIMEOUT) {
        CmdTimeout += CmdTimeout;
      } else {
        if (Packet->SdMmcCmdBlk != NULL) {
          XenonReset (Private, Slot, SDHC_CMD_RESET_BIT);
        }

        if (DataTransfer) {
          XenonReset (Private, Slot, SDHC_DATA_RESET_BIT);
        }

        DEBUG((DEBUG_ERROR, "MvSdMmcPassThru: Timeout...\n"));

        return EFI_TIMEOUT;
      }
    }

    Time++;
    // Poll interval for data/command inhibit is 1ms
    gBS->Stall (1000);

    SdMmcHcRwMmio (
            Private->PciIo,
            Slot,
            SD_MMC_HC_PRESENT_STATE,
            TRUE,
            sizeof (PresentState),
            &PresentState
          );
  }

  return EFI_SUCCESS;
}

// Variable to store current card's Rca - used in polling card status
STATIC UINT16 mCurrRca = 0;

/**
  Sends SD command to an SD card that is attached to the SD controller.

  The PassThru() function sends the SD command specified by Packet to the SD card
  specified by Slot.

  If Packet is successfully sent to the SD card, then EFI_SUCCESS is returned.

  If a device error occurs while sending the Packet, then EFI_DEVICE_ERROR is returned.

  If Slot is not in a valid range for the SD controller, then EFI_INVALID_PARAMETER
  is returned.

  If Packet defines a data command but both InDataBuffer and OutDataBuffer are NULL,
  EFI_INVALID_PARAMETER is returned.

  @param[in]     This           A pointer to the EFI_SD_MMC_PASS_THRU_PROTOCOL instance.
  @param[in]     Slot           The slot number of the SD card to send the command to.
  @param[in,out] Packet         A pointer to the SD command data structure.
  @param[in]     Event          If Event is NULL, blocking I/O is performed. If Event is
                                not NULL, then nonblocking I/O is performed, and Event
                                will be signaled when the Packet completes.

  @retval EFI_SUCCESS           The SD Command Packet was sent by the host.
  @retval EFI_DEVICE_ERROR      A device error occurred while attempting to send the SD
                                command Packet.
  @retval EFI_INVALID_PARAMETER Packet, Slot, or the contents of the Packet is invalid.
  @retval EFI_INVALID_PARAMETER Packet defines a data command but both InDataBuffer and
                                OutDataBuffer are NULL.
  @retval EFI_NO_MEDIA          SD Device not present in the Slot.
  @retval EFI_UNSUPPORTED       The command described by the SD Command Packet is not
                                supported by the host controller.
  @retval EFI_BAD_BUFFER_SIZE   The InTransferLength or OutTransferLength exceeds the
                                limit supported by SD card ( i.e. if the number of bytes
                                exceed the Last LBA).

**/
EFI_STATUS
EFIAPI
SdMmcPassThruPassThru (
  IN     EFI_SD_MMC_PASS_THRU_PROTOCOL         *This,
  IN     UINT8                                 Slot,
  IN OUT EFI_SD_MMC_PASS_THRU_COMMAND_PACKET   *Packet,
  IN     EFI_EVENT                             Event    OPTIONAL
  )
{
  EFI_STATUS Status;
  SD_MMC_HC_PRIVATE_DATA *Private;
  INTN Ret;
  UINTN *Data, DataLen, Index;
  UINT32 Argument, Response[4], Retry, Mask;
  UINT16 BlockSize, BlkCount;
  UINT16 Cmd, IntStatus, TimeoutCtrl, TmpIntStat, Mode, BlkSizeReg;
  BOOLEAN MultiFlag = FALSE, SetRcaFlag = FALSE, Read = FALSE;
  BOOLEAN DataTransfer;


  Data = NULL;
  DataLen = 0;
  Retry = SDHC_INT_STATUS_POLL_RETRY;
  Ret = 0;
  BlockSize = SIZE_512B;
  BlkCount = 0;

  Status = CheckParameters (This, Packet, Slot);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  //
  // Mark if this is multiblock data transfer since some special
  // acctions must be taken in such circumstances, i.e. it's necessary
  // to send SD_STOP_TRANSMISSION command.
  //
  if (Packet->SdMmcCmdBlk->CommandIndex == SD_WRITE_MULTIPLE_BLOCK ||
      Packet->SdMmcCmdBlk->CommandIndex == SD_READ_MULTIPLE_BLOCK) {
    MultiFlag = TRUE;
  }

  //
  // Mark if this is command to set new relative card address (RCA),
  // since it's necessary to have updated copy of current RCA in this
  // layer - used in checking card status.
  //
  if (Packet->SdMmcCmdBlk->CommandIndex == SD_SET_RELATIVE_ADDR) {
    SetRcaFlag = TRUE;
  }

  // Prepare data transfer's flags
  if (((Packet->InTransferLength != 0) && (Packet->InDataBuffer != NULL)) ||
      ((Packet->OutTransferLength != 0) && (Packet->OutDataBuffer != NULL))) {
    DataTransfer = FALSE;
  }

  if ((Packet->InTransferLength != 0) && (Packet->InDataBuffer != NULL)) {
    DataLen = Packet->InTransferLength;
    Data = Packet->InDataBuffer;
    Read = TRUE;
    DataTransfer = TRUE;
  }

  if ((Packet->OutTransferLength != 0) && (Packet->OutDataBuffer != NULL)) {
    DataLen = Packet->OutTransferLength;
    Data = Packet->OutDataBuffer;
    DataTransfer = TRUE;
  }

  // Clear ERROR_IRQ status
  IntStatus = SDHC_CLR_IRQ_STS_MASK;
  Status = SdMmcHcRwMmio (
                   Private->PciIo,
                   Slot,
                   SD_MMC_HC_ERR_INT_STS,
                   FALSE,
                   sizeof (IntStatus),
                   &IntStatus
                 );

  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "MvSdMmcPassThru: Cannot clear ERROR_IRQ status\n"));
    return Status;
  }

  // Clear IRQ status
  Status = SdMmcHcRwMmio (
                   Private->PciIo,
                   Slot,
                   SD_MMC_HC_NOR_INT_STS,
                   FALSE,
                   sizeof (IntStatus),
                   &IntStatus
                 );

  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "MvSdMmcPassThru: Cannot clear IRQ status\n"));
    return Status;
  }

  WaitForInhibit (Private, Packet, Slot, DataTransfer);

  Cmd = (UINT16)LShiftU64(Packet->SdMmcCmdBlk->CommandIndex, 8);
  if (Packet->SdMmcCmdBlk->CommandType == SdMmcCommandTypeAdtc) {
    Cmd |= DATA_PRESENT;
  }

  if (Packet->SdMmcCmdBlk->CommandType != SdMmcCommandTypeBc) {
    switch (Packet->SdMmcCmdBlk->ResponseType) {
      case SdMmcResponseTypeR1:
      case SdMmcResponseTypeR5:
      case SdMmcResponseTypeR6:
      case SdMmcResponseTypeR7:
        Cmd |= (RESP_TYPE_48_BITS | CMD_CRC_CHK_EN | CMD_INDEX_CHK_EN);
        break;
      case SdMmcResponseTypeR2:
        Cmd |= (RESP_TYPE_136_BITS | CMD_CRC_CHK_EN);
       break;
      case SdMmcResponseTypeR3:
      case SdMmcResponseTypeR4:
        Cmd |= RESP_TYPE_48_BITS;
        break;
      case SdMmcResponseTypeR1b:
      case SdMmcResponseTypeR5b:
        Cmd |= (RESP_TYPE_48_BITS_NO_CRC | CMD_CRC_CHK_EN | CMD_INDEX_CHK_EN);
        break;
      default:
        ASSERT (FALSE);
        return EFI_INVALID_PARAMETER;
        break;
    }
  }

  if ((DataLen % BlockSize) != 0) {
    if (DataLen < BlockSize) {
      BlockSize = (UINT16)DataLen;
    }
  }
  BlkCount = (UINT16) (DataLen / BlockSize);

  if (DataTransfer) {

    // Set default timeout value
    TimeoutCtrl = DATA_TIMEOUT_DEF_VAL;
    SdMmcHcRwMmio (
             Private->PciIo,
             Slot,
             SD_MMC_HC_TIMEOUT_CTRL,
             FALSE,
             sizeof (TimeoutCtrl),
             &TimeoutCtrl
           );

    Mode = SDHC_TRNS_BLK_CNT_EN;
    if (BlkCount > 1) {
      Mode |= SDHC_TRNS_MULTI_BLK_SEL;
    }
    if (Read) {
      Mode |= SDHC_TRNS_TO_HOST_DIR;
    }

    // Fill BlockSize register
    BlkSizeReg = (BLK_SIZE_512KB << BLK_SIZE_HOST_DMA_BDRY_OFFSET) | (BlockSize & (SIZE_4KB -1));
    SdMmcHcRwMmio (
             Private->PciIo,
             Slot,
             SD_MMC_HC_BLK_SIZE,
             FALSE,
             sizeof (BlkSizeReg),
             &BlkSizeReg
           );

    SdMmcHcRwMmio (
             Private->PciIo,
             Slot,
             SD_MMC_HC_BLK_COUNT,
             FALSE,
             sizeof (BlkCount),
             &BlkCount
           );

    SdMmcHcRwMmio (
             Private->PciIo,
             Slot,
             SD_MMC_HC_TRANS_MOD,
             FALSE,
             sizeof (Mode),
             &Mode
           );
  }

  Argument = Packet->SdMmcCmdBlk->CommandArgument;
  SdMmcHcRwMmio (
           Private->PciIo,
           Slot,
           SD_MMC_HC_ARG1,
           FALSE,
           sizeof (Argument),
           &Argument
         );

  // Execute cmd
  SdMmcHcRwMmio (
           Private->PciIo,
           Slot,
           SD_MMC_HC_COMMAND,
           FALSE,
           sizeof (Cmd),
           &Cmd
         );

  Mask = NOR_INT_STS_CMD_COMPLETE;
  if (Packet->SdMmcCmdBlk->ResponseType == SdMmcResponseTypeR1b ||
      Packet->SdMmcCmdBlk->ResponseType == SdMmcResponseTypeR5b) {
    Mask |= NOR_INT_STS_XFER_COMPLETE;
  }

  do {
    Status = SdMmcHcRwMmio (
                     Private->PciIo,
                     Slot,
                     SD_MMC_HC_NOR_INT_STS,
                     TRUE,
                     sizeof (IntStatus),
                     &IntStatus
                   );

    if (EFI_ERROR (Status)) {
      return EFI_INVALID_PARAMETER;
    }

    if (IntStatus & NOR_INT_STS_ERR_INT) {
      DEBUG((DEBUG_INFO, "MvSdMmcPassThru: SDHCI_INT_ERROR stat:%x\n",
        IntStatus));
      break;
    }

    // Int Status Register poll interval is 100us according to SDHCI spec
    gBS->Stall (100);
    if (--Retry == 0) {
      break;
    }
  } while ((IntStatus & Mask) != Mask);

  if (Retry == 0) {
    DEBUG((DEBUG_ERROR, "MvSdMmcPassThru: Rx timeout\n"));
    return EFI_TIMEOUT;
  }

  if ((IntStatus & (NOR_INT_STS_ERR_INT | Mask)) == Mask) {
    if (Packet->SdMmcCmdBlk->CommandType != SdMmcCommandTypeBc) {
      for (Index = 0; Index < 4; Index++) {
        SdMmcHcRwMmio (
                Private->PciIo,
                Slot,
                SD_MMC_HC_RESPONSE + Index * 4,
                TRUE,
                sizeof (Response[Index]),
                &Response[Index]
              );
      }

      CopyMem (Packet->SdMmcStatusBlk, Response, sizeof (Response));

      //
      // Update RCA of current card in order to use it later
      // for polling card status
      //
      if (SetRcaFlag) {
        mCurrRca = Packet->SdMmcStatusBlk->Resp0 >> RCA_BITS_OFFSET;
      }
    }

    IntStatus = Mask;
    Status = SdMmcHcRwMmio (
                     Private->PciIo,
                     Slot,
                     SD_MMC_HC_NOR_INT_STS,
                     FALSE,
                     sizeof (IntStatus),
                     &IntStatus
                   );

  } else {
    Ret = -1;
  }

  if (!Ret && DataTransfer) {
    Status = XenonTransferData (Private, Slot, Data, DataLen, BlockSize, BlkCount, Read);
  }

  // Save current IRQ status
  Status = SdMmcHcRwMmio (
                   Private->PciIo,
                   Slot,
                   SD_MMC_HC_NOR_INT_STS,
                   TRUE,
                   sizeof (IntStatus),
                   &IntStatus
                 );

  // Clear IRQ
  TmpIntStat = SDHC_CLR_IRQ_STS_MASK;
  Status = SdMmcHcRwMmio (
                   Private->PciIo,
                   Slot,
                   SD_MMC_HC_NOR_INT_STS,
                   FALSE,
                   sizeof (TmpIntStat),
                   &TmpIntStat
                 );

  //
  // There are two quirks for DataTransfer commands:
  // 1. For multipleblock write/read it's necessary to issue
  // SD_STOP_TRANSMISSION command
  // 2. For every write data comand, it's necessary to poll for
  // card status before issuing next data transfer
  //
  if (!Ret && DataTransfer) {
    if (MultiFlag) {
      SdMmcSendStopTransmission(This, Slot);
    }

    if (!Read) {
      //
      // Poll max 1000 times for device to be ready for next
      // transfer (buffer is empty after previous transfer
      //
      return SdMmcIsReady (This, Slot, mCurrRca, 1000);
    }

    return EFI_SUCCESS;
  }

  // Check previously saved IRQ status for non-data commands
  if (IntStatus & NOR_INT_STS_ERR_INT) {
    if (Packet->SdMmcCmdBlk != NULL) {
      XenonReset (Private, Slot, SDHC_CMD_RESET_BIT);
    }

    if (DataTransfer) {
      XenonReset (Private, Slot, SDHC_DATA_RESET_BIT);
    }

    if (IntStatus & (ERR_INT_STS_CMD_TIMEOUT_ERR | ERR_INT_STS_DATA_TIMEOUT_ERR)) {
      DEBUG((DEBUG_ERROR, "MvSdMmcPassThru: Timeout!\n"));
      return EFI_TIMEOUT;
    } else {
      return EFI_DEVICE_ERROR;
    }
  } else {
    return EFI_SUCCESS;
  }

  return Status;
}

/**
  Used to retrieve next slot numbers supported by the SD controller. The function
  returns information about all available slots (populated or not-populated).

  The GetNextSlot() function retrieves the next slot number on an SD controller.
  If on input Slot is 0xFF, then the slot number of the first slot on the SD controller
  is returned.

  If Slot is a slot number that was returned on a previous call to GetNextSlot(), then
  the slot number of the next slot on the SD controller is returned.

  If Slot is not 0xFF and Slot was not returned on a previous call to GetNextSlot(),
  EFI_INVALID_PARAMETER is returned.

  If Slot is the slot number of the last slot on the SD controller, then EFI_NOT_FOUND
  is returned.

  @param[in]     This           A pointer to the EFI_SD_MMMC_PASS_THRU_PROTOCOL instance.
  @param[in,out] Slot           On input, a pointer to a slot number on the SD controller.
                                On output, a pointer to the next slot number on the SD controller.
                                An input value of 0xFF retrieves the first slot number on the SD
                                controller.

  @retval EFI_SUCCESS           The next slot number on the SD controller was returned in Slot.
  @retval EFI_NOT_FOUND         There are no more slots on this SD controller.
  @retval EFI_INVALID_PARAMETER Slot is not 0xFF and Slot was not returned on a previous call
                                to GetNextSlot().

**/
EFI_STATUS
EFIAPI
SdMmcPassThruGetNextSlot (
  IN     EFI_SD_MMC_PASS_THRU_PROTOCOL        *This,
  IN OUT UINT8                                *Slot
  )
{
  SD_MMC_HC_PRIVATE_DATA          *Private;
  UINT8                           Index;

  if ((This == NULL) || (Slot == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  if (*Slot == 0xFF) {
    for (Index = 0; Index < SD_MMC_HC_MAX_SLOT; Index++) {
      if (Private->Slot[Index].Enable) {
        *Slot = Index;
        Private->PreviousSlot = Index;
        return EFI_SUCCESS;
      }
    }
    return EFI_NOT_FOUND;
  } else if (*Slot == Private->PreviousSlot) {
    for (Index = *Slot + 1; Index < SD_MMC_HC_MAX_SLOT; Index++) {
      if (Private->Slot[Index].Enable) {
        *Slot = Index;
        Private->PreviousSlot = Index;
        return EFI_SUCCESS;
      }
    }
    return EFI_NOT_FOUND;
  } else {
    return EFI_INVALID_PARAMETER;
  }
}

/**
  Used to allocate and build a device path node for an SD card on the SD controller.

  The BuildDevicePath() function allocates and builds a single device node for the SD
  card specified by Slot.

  If the SD card specified by Slot is not present on the SD controller, then EFI_NOT_FOUND
  is returned.

  If DevicePath is NULL, then EFI_INVALID_PARAMETER is returned.

  If there are not enough resources to allocate the device path node, then EFI_OUT_OF_RESOURCES
  is returned.

  Otherwise, DevicePath is allocated with the boot service AllocatePool(), the contents of
  DevicePath are initialized to describe the SD card specified by Slot, and EFI_SUCCESS is
  returned.

  @param[in]     This           A pointer to the EFI_SD_MMMC_PASS_THRU_PROTOCOL instance.
  @param[in]     Slot           Specifies the slot number of the SD card for which a device
                                path node is to be allocated and built.
  @param[in,out] DevicePath     A pointer to a single device path node that describes the SD
                                card specified by Slot. This function is responsible for
                                allocating the buffer DevicePath with the boot service
                                AllocatePool(). It is the caller's responsibility to free
                                DevicePath when the caller is finished with DevicePath.

  @retval EFI_SUCCESS           The device path node that describes the SD card specified by
                                Slot was allocated and returned in DevicePath.
  @retval EFI_NOT_FOUND         The SD card specified by Slot does not exist on the SD controller.
  @retval EFI_INVALID_PARAMETER DevicePath is NULL.
  @retval EFI_OUT_OF_RESOURCES  There are not enough resources to allocate DevicePath.

**/
EFI_STATUS
EFIAPI
SdMmcPassThruBuildDevicePath (
  IN     EFI_SD_MMC_PASS_THRU_PROTOCOL       *This,
  IN     UINT8                               Slot,
  IN OUT EFI_DEVICE_PATH_PROTOCOL            **DevicePath
  )
{
  SD_MMC_HC_PRIVATE_DATA          *Private;
  SD_DEVICE_PATH                  *SdNode;
  EMMC_DEVICE_PATH                *EmmcNode;

  if ((This == NULL) || (DevicePath == NULL) || (Slot >= SD_MMC_HC_MAX_SLOT)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  if ((!Private->Slot[Slot].Enable) || (!Private->Slot[Slot].MediaPresent)) {
    return EFI_NOT_FOUND;
  }

  if (Private->Slot[Slot].CardType == SdCardType) {
    SdNode = AllocateCopyPool (sizeof (SD_DEVICE_PATH), &mSdDpTemplate);
    if (SdNode == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    SdNode->SlotNumber = Slot;

    *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) SdNode;
  } else if (Private->Slot[Slot].CardType == EmmcCardType) {
    EmmcNode = AllocateCopyPool (sizeof (EMMC_DEVICE_PATH), &mEmmcDpTemplate);
    if (EmmcNode == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    EmmcNode->SlotNumber = Slot;

    *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) EmmcNode;
  } else {
    //
    // Currently we only support SD and EMMC two device nodes.
    //
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/**
  This function retrieves an SD card slot number based on the input device path.

  The GetSlotNumber() function retrieves slot number for the SD card specified by
  the DevicePath node. If DevicePath is NULL, EFI_INVALID_PARAMETER is returned.

  If DevicePath is not a device path node type that the SD Pass Thru driver supports,
  EFI_UNSUPPORTED is returned.

  @param[in]  This              A pointer to the EFI_SD_MMC_PASS_THRU_PROTOCOL instance.
  @param[in]  DevicePath        A pointer to the device path node that describes a SD
                                card on the SD controller.
  @param[out] Slot              On return, points to the slot number of an SD card on
                                the SD controller.

  @retval EFI_SUCCESS           SD card slot number is returned in Slot.
  @retval EFI_INVALID_PARAMETER Slot or DevicePath is NULL.
  @retval EFI_UNSUPPORTED       DevicePath is not a device path node type that the SD
                                Pass Thru driver supports.

**/
EFI_STATUS
EFIAPI
SdMmcPassThruGetSlotNumber (
  IN  EFI_SD_MMC_PASS_THRU_PROTOCOL          *This,
  IN  EFI_DEVICE_PATH_PROTOCOL               *DevicePath,
  OUT UINT8                                  *Slot
  )
{
  SD_MMC_HC_PRIVATE_DATA          *Private;
  SD_DEVICE_PATH                  *SdNode;
  EMMC_DEVICE_PATH                *EmmcNode;
  UINT8                           SlotNumber;

  if ((This == NULL) || (DevicePath == NULL) || (Slot == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  //
  // Check whether the DevicePath belongs to SD_DEVICE_PATH or EMMC_DEVICE_PATH
  //
  if ((DevicePath->Type != MESSAGING_DEVICE_PATH) ||
      ((DevicePath->SubType != MSG_SD_DP) &&
       (DevicePath->SubType != MSG_EMMC_DP)) ||
      (DevicePathNodeLength(DevicePath) != sizeof(SD_DEVICE_PATH)) ||
      (DevicePathNodeLength(DevicePath) != sizeof(EMMC_DEVICE_PATH))) {
    return EFI_UNSUPPORTED;
  }

  if (DevicePath->SubType == MSG_SD_DP) {
    SdNode = (SD_DEVICE_PATH *) DevicePath;
    SlotNumber = SdNode->SlotNumber;
  } else {
    EmmcNode = (EMMC_DEVICE_PATH *) DevicePath;
    SlotNumber = EmmcNode->SlotNumber;
  }

  if (SlotNumber >= SD_MMC_HC_MAX_SLOT) {
    return EFI_NOT_FOUND;
  }

  if (Private->Slot[SlotNumber].Enable) {
    *Slot = SlotNumber;
    return EFI_SUCCESS;
  } else {
    return EFI_NOT_FOUND;
  }
}

/**
  Resets an SD card that is connected to the SD controller.

  The ResetDevice() function resets the SD card specified by Slot.

  If this SD controller does not support a device reset operation, EFI_UNSUPPORTED is
  returned.

  If Slot is not in a valid slot number for this SD controller, EFI_INVALID_PARAMETER
  is returned.

  If the device reset operation is completed, EFI_SUCCESS is returned.

  @param[in]  This              A pointer to the EFI_SD_MMC_PASS_THRU_PROTOCOL instance.
  @param[in]  Slot              Specifies the slot number of the SD card to be reset.

  @retval EFI_SUCCESS           The SD card specified by Slot was reset.
  @retval EFI_UNSUPPORTED       The SD controller does not support a device reset operation.
  @retval EFI_INVALID_PARAMETER Slot number is invalid.
  @retval EFI_NO_MEDIA          SD Device not present in the Slot.
  @retval EFI_DEVICE_ERROR      The reset command failed due to a device error

**/
EFI_STATUS
EFIAPI
SdMmcPassThruResetDevice (
  IN EFI_SD_MMC_PASS_THRU_PROTOCOL           *This,
  IN UINT8                                   Slot
  )
{
  SD_MMC_HC_PRIVATE_DATA          *Private;
  LIST_ENTRY                      *Link;
  LIST_ENTRY                      *NextLink;
  SD_MMC_HC_TRB                   *Trb;
  EFI_TPL                         OldTpl;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SD_MMC_HC_PRIVATE_FROM_THIS (This);

  if (!Private->Slot[Slot].Enable) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->Slot[Slot].MediaPresent) {
    return EFI_NO_MEDIA;
  }

  if (!Private->Slot[Slot].Initialized) {
    return EFI_DEVICE_ERROR;
  }
  //
  // Free all async I/O requests in the queue
  //
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  for (Link = GetFirstNode (&Private->Queue);
       !IsNull (&Private->Queue, Link);
       Link = NextLink) {
    NextLink = GetNextNode (&Private->Queue, Link);
    RemoveEntryList (Link);
    Trb = SD_MMC_HC_TRB_FROM_THIS (Link);
    Trb->Packet->TransactionStatus = EFI_ABORTED;
    gBS->SignalEvent (Trb->Event);
    SdMmcFreeTrb (Trb);
  }

  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}
