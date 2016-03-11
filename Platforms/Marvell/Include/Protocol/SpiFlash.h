/*******************************************************************************
Copyright (C) 2016 Marvell International Ltd.

Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or
modify this File under the following licensing terms.
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.

	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.

	* Neither the name of Marvell nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/
#ifndef __EFI_SPI_FLASH_H__
#define __EFI_SPI_FLASH_H__

#include <Protocol/Spi.h>

extern EFI_GUID gEfiSpiFlashProtocolGuid;

typedef struct _EFI_SPI_FLASH_PROTOCOL EFI_SPI_FLASH_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_INIT) (
  IN EFI_SPI_FLASH_PROTOCOL *This,
  IN SPI_DEVICE             *SpiDev
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_READ_ID) (
  IN     SPI_DEVICE *SpiDev,
  IN     UINT32     DataByteCount,
  IN OUT UINT8      *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_READ) (
  IN SPI_DEVICE *SpiDev,
  IN UINT32     Address,
  IN UINTN      DataByteCount,
  IN VOID       *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_WRITE) (
  IN SPI_DEVICE *SpiDev,
  IN UINT32     Address,
  IN UINTN      DataByteCount,
  IN VOID       *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_ERASE) (
  IN SPI_DEVICE *SpiDev,
  IN UINTN      Address,
  IN UINTN      DataByteCount
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SPI_FLASH_UPDATE) (
  IN SPI_DEVICE *SpiDev,
  IN UINT32     Address,
  IN UINTN      DataByteCount,
  IN UINT8      *Buffer
  );

struct _EFI_SPI_FLASH_PROTOCOL {
  EFI_SPI_FLASH_INIT    Init;
  EFI_SPI_FLASH_READ_ID ReadId;
  EFI_SPI_FLASH_READ    Read;
  EFI_SPI_FLASH_WRITE   Write;
  EFI_SPI_FLASH_ERASE   Erase;
  EFI_SPI_FLASH_UPDATE  Update;
};

#endif // __EFI_SPI_FLASH_H__