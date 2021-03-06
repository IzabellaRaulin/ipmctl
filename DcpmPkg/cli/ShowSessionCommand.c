/*
 * Copyright (c) 2018, Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Library/ShellLib.h>
#include <Library/BaseMemoryLib.h>
#include "ShowSessionCommand.h"
#include <Debug.h>
#include <Types.h>
#include <NvmInterface.h>
#include <NvmLimits.h>
#include <Convert.h>
#include "Common.h"
#include <Utility.h>
#include <PbrDcpmm.h>

#define DS_ROOT_PATH                        L"/Session"
#define DS_TAG_PATH                         L"/Session/Tag"
#define DS_TAG_INDEX_PATH                   L"/Session/Tag[%d]"

#define TAG_ID_FORMAT                       L"0x%x"
#define TAG_ID_SELECTED_FORMAT              L"0x%x*"

EFI_STATUS
MapTagtoCurrentSessionState(
  IN  EFI_DCPMM_CONFIG_PROTOCOL *pNvmDimmConfigProtocol,
  OUT UINT32 *pTag
);

 /*
  *  PRINT LIST ATTRIBUTES
  *  ---TagId=0x0001---
  *     ExitCode=0
  *     CliArgs=
  */
PRINTER_LIST_ATTRIB ShowSessionListAttributes =
{
 {
    {
      TAG_STR,                                            //GROUP LEVEL TYPE
      L"---" TAG_ID_STR L"=$(" TAG_ID_STR L")---",        //NULL or GROUP LEVEL HEADER
      SHOW_LIST_IDENT L"%ls=%ls",                         //NULL or KEY VAL FORMAT STR
      TAG_ID_STR                                          //NULL or IGNORE KEY LIST (K1;K2)
    }
  }
};

 /*
 *  PRINTER TABLE ATTRIBUTES (3 columns)
 *   TagID  | ExitCode | CliArgs
 *   ========================================================================
 *   0x0001 | X        | X      
 *   ...
 */
PRINTER_TABLE_ATTRIB ShowSessionTableAttributes =
{
  {
    {
      TAG_ID_STR,                                 //COLUMN HEADER
      DEFAULT_MAX_STR_WIDTH,                      //COLUMN MAX STR WIDTH
      DS_TAG_PATH PATH_KEY_DELIM TAG_ID_STR       //COLUMN DATA PATH
    },
    {
      EXIT_CODE_STR,                              //COLUMN HEADER
      DEFAULT_MAX_STR_WIDTH,                      //COLUMN MAX STR WIDTH
      DS_TAG_PATH PATH_KEY_DELIM EXIT_CODE_STR    //COLUMN DATA PATH
    },
    {
      CLI_ARGS_STR,                               //COLUMN HEADER
      DEFAULT_MAX_STR_WIDTH,                      //COLUMN MAX STR WIDTH
      DS_TAG_PATH PATH_KEY_DELIM CLI_ARGS_STR     //COLUMN DATA PATH
    }
  }
};

PRINTER_DATA_SET_ATTRIBS ShowSessionDataSetAttribs =
{
  &ShowSessionListAttributes,
  &ShowSessionTableAttributes
};

/**
  Command syntax definition
**/
struct Command ShowSessionCommand = {
  SHOW_VERB,                                                                                    //!< verb
  {
#ifdef OS_BUILD
    { OUTPUT_OPTION_SHORT, OUTPUT_OPTION, L"", OUTPUT_OPTION_HELP, FALSE, ValueRequired },
#endif
    {L"", L"", L"", FALSE, ValueOptional}
  },   //!< options
  {{SESSION_TARGET, L"", L"", TRUE, ValueEmpty}},                                                //!< targets
  {{L"", L"", L"", FALSE, ValueOptional}},                                                      //!< properties
  L"Show basic information about session pbr file",                                             //!< help
  ShowSession,
  TRUE,
  TRUE //exclude from PBR
};


/**
  Execute the show host server command

  @param[in] pCmd command from CLI

  @retval EFI_SUCCESS success
  @retval EFI_INVALID_PARAMETER pCmd is NULL or invalid command line parameters
  @retval EFI_OUT_OF_RESOURCES memory allocation failure
  @retval EFI_ABORTED invoking CONFIG_PROTOGOL function failure
**/
EFI_STATUS
ShowSession(
  IN     struct Command *pCmd
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  EFI_DCPMM_CONFIG_PROTOCOL *pNvmDimmConfigProtocol = NULL;
  DISPLAY_PREFERENCES DisplayPreferences;
  PRINT_CONTEXT *pPrinterCtx = NULL;
  CHAR16 *pPath = NULL;
  UINT32 TagCount = 0;
  UINT32 Index = 0;
  CHAR16 *pName = NULL;
  CHAR16 *pDescription = NULL;
  CHAR16 *pTagId = NULL;
  UINT32 TagId = INVALID_TAG_ID;
  UINT32 Signature;

  NVDIMM_ENTRY();

  ZeroMem(&DisplayPreferences, sizeof(DisplayPreferences));

  if (pCmd == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_NO_COMMAND);
    goto Finish;
  }

  pPrinterCtx = pCmd->pPrintCtx;

  ReturnCode = ReadRunTimeCliDisplayPreferences(&DisplayPreferences);
  if (EFI_ERROR(ReturnCode)) {
    ReturnCode = EFI_NOT_FOUND;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_DISPLAY_PREFERENCES_RETRIEVE);
    goto Finish;
  }

  /**
    Make sure we can access the config protocol
  **/
  ReturnCode = OpenNvmDimmProtocol(gNvmDimmConfigProtocolGuid, (VOID **)&pNvmDimmConfigProtocol, NULL);
  if (EFI_ERROR(ReturnCode)) {
    ReturnCode = EFI_NOT_FOUND;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_OPENING_CONFIG_PROTOCOL);
    goto Finish;
  }
  /*
  ReturnCode = pNvmDimmConfigProtocol->PbrGetCurrentTag(&TagId, NULL, NULL, NULL);
  if (EFI_ERROR(ReturnCode) && ReturnCode != EFI_NOT_FOUND) {
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_FAILED_TO_GET_SESSION_TAG);
    goto Finish;
  }*/

  MapTagtoCurrentSessionState(pNvmDimmConfigProtocol, &TagId);

  ReturnCode = pNvmDimmConfigProtocol->PbrGetTagCount(&TagCount);
  if (EFI_ERROR(ReturnCode)) {
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_FAILED_TO_GET_SESSION_TAG_COUNT);
    goto Finish;
  }

  for (Index = 0; Index < TagCount; ++Index) {
    if (Index == TagId) {
      pTagId = CatSPrintClean(NULL, TAG_ID_SELECTED_FORMAT, Index);
    }
    else {
      pTagId = CatSPrintClean(NULL, TAG_ID_FORMAT, Index);
    }
    
    PRINTER_BUILD_KEY_PATH(pPath, DS_TAG_INDEX_PATH, Index);

    ReturnCode = pNvmDimmConfigProtocol->PbrGetTag(Index, &Signature, &pName, &pDescription, NULL, NULL);
    if (ReturnCode == EFI_SUCCESS) {

      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, TAG_ID_STR, pTagId);
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, EXIT_CODE_STR, pDescription);
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, CLI_ARGS_STR, pName);
    }
    FREE_POOL_SAFE(pTagId);
    FREE_POOL_SAFE(pName);
    FREE_POOL_SAFE(pDescription);
  }

  //Specify table attributes
  PRINTER_CONFIGURE_DATA_ATTRIBUTES(pPrinterCtx, DS_ROOT_PATH, &ShowSessionDataSetAttribs);
  PRINTER_ENABLE_TEXT_TABLE_FORMAT(pPrinterCtx);

Finish:
  PRINTER_PROCESS_SET_BUFFER(pPrinterCtx);
  NVDIMM_EXIT_I64(ReturnCode);
  FREE_POOL_SAFE(pPath);
  FREE_POOL_SAFE(pTagId);
  FREE_POOL_SAFE(pName);
  FREE_POOL_SAFE(pDescription);
  return  ReturnCode;
}

/**
  Register the show session command

  @retval EFI_SUCCESS success
  @retval EFI_ABORTED registering failure
  @retval EFI_OUT_OF_RESOURCES memory allocation failure
**/
EFI_STATUS
RegisterShowSessionCommand(
  EFI_DCPMM_CONFIG_PROTOCOL *pNvmDimmConfigProtocol
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  NVDIMM_ENTRY();

  ReturnCode = RegisterCommand(&ShowSessionCommand);

  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

EFI_STATUS
MapTagtoCurrentSessionState(
  IN  EFI_DCPMM_CONFIG_PROTOCOL *pNvmDimmConfigProtocol,
  OUT UINT32 *pTag
)
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT32 TagCount = 0;
  UINT32 Index = 0;
  CHAR16 *pName = NULL;
  CHAR16 *pDescription = NULL;
  TagPartitionInfo *pTagPartitionInfo = NULL;
  UINT32 TagPartitionCount = 0;
  UINT32 Signature;
  UINT32 TotalDataItems = 0;
  UINT32 TotalDataSize = 0;
  UINT32 CurrentPbOffset = 0;
  UINT32 TagPartIndex = 0;

  ReturnCode = pNvmDimmConfigProtocol->PbrGetDataPlaybackInfo(
    PBR_PASS_THRU_SIG,
    &TotalDataItems,
    &TotalDataSize,
    &CurrentPbOffset);

  if (EFI_ERROR(ReturnCode)) {
    goto Finish;
  }

  if (0 == CurrentPbOffset) {
    *pTag = 0;
    return EFI_SUCCESS;
  }
  ReturnCode = pNvmDimmConfigProtocol->PbrGetTagCount(&TagCount);
  if (EFI_ERROR(ReturnCode)) {
    goto Finish;
  }

  for (Index = 0; Index < TagCount; ++Index) {
    ReturnCode = pNvmDimmConfigProtocol->PbrGetTag(Index, &Signature, &pName, &pDescription, (VOID**)&pTagPartitionInfo, &TagPartitionCount);
    if (EFI_ERROR(ReturnCode)) {
      goto Finish;
    }

    if (PBR_DCPMM_CLI_SIG == Signature) {
      for (TagPartIndex = 0; TagPartIndex < TagPartitionCount; ++TagPartIndex) {
        if (PBR_PASS_THRU_SIG == pTagPartitionInfo[TagPartIndex].PartitionSignature)
        {
          if (CurrentPbOffset == pTagPartitionInfo[TagPartIndex].PartitionCurrentOffset) {
            *pTag = Index;
            ReturnCode = EFI_SUCCESS;
            goto Finish;
          }
        }
      }
    }

    FREE_POOL_SAFE(pName);
    FREE_POOL_SAFE(pDescription);
    FREE_POOL_SAFE(pTagPartitionInfo);
  }
Finish:
  FREE_POOL_SAFE(pName);
  FREE_POOL_SAFE(pDescription);
  FREE_POOL_SAFE(pTagPartitionInfo);
  return ReturnCode;
}