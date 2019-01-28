/******************************************************************************
* Copyright (C) 2018-2019 Xilinx, Inc. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xplmi_cdo.c
*
* This file contains code to handling the CDO Buffer.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date        Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00  kc   08/23/2018 Initial release
*
* </pre>
*
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xplmi_cdo.h"

/************************** Constant Definitions *****************************/
#define XPLMI_CMD_LEN_TEMPBUF		(0x8U)

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/*****************************************************************************/

/*****************************************************************************/
/**
 * @brief
 *
 * @param
 *
 * @return
 *
 *****************************************************************************/
static u32 XPlmi_CmdSize(u32 *Buf, u32 Len)
{
	u32 Size = 1;
	if (Len >= Size)
	{
		u32 CmdId = Buf[0];
		u32 PayloadLen = (CmdId & XPLMI_CMD_LEN_MASK) >> 16;
		if (PayloadLen == 255)
		{
			Size = 2;
			if (Len >= Size)
			{
				PayloadLen = Buf[1];
			}
		}
		Size += PayloadLen;
	}
	return Size;
}

/*****************************************************************************/
/**
 * @brief This function will setup the command structure
 *
 * @param Cmd Pointer to the command structure
 * @param Buf Pointer to the command buffer
 * @param BufLen length of the buffer. It may not have total Command length if
 * buffer is not available.
 *
 * @return None
 *
 *****************************************************************************/
static void XPlmi_SetupCmd(XPlmi_Cmd * Cmd, u32 *Buf, u32 BufLen)
{
	Cmd->CmdId = Buf[0];
	Cmd->Len = (Cmd->CmdId >> 16) & 255;
	Cmd->Payload = Buf + 1;
	if (Cmd->Len == 255) {
		Cmd->Len = Buf[1];
		Cmd->Payload = Buf + 2;
	}

	/** assign the available payloadlen in the buffer */
	if (Cmd->Len > BufLen)
	{
		Cmd->PayloadLen =  BufLen;
	} else {
		Cmd->PayloadLen =  Cmd->Len;
	}
}

/*****************************************************************************/
/**
 * @brief This function verifies CDO header
 *
 * @param Pointer to the CDO structure
 *
 * @return SUCCESS if header is as per specification.
 *
 *****************************************************************************/
int XPlmi_CdoVerifyHeader(XPlmiCdo *CdoPtr)
{
	u32 CheckSum=0U;
	u32 *CdoHdr=CdoPtr->BufPtr;
	u32 Index;
	XStatus Status;

	if (CdoHdr[1] != XPLMI_CDO_HDR_IDN_WRD)
	{
		Status = XST_FAILURE;
		goto END;
	}

	for (Index=0U;Index<(XPLMI_CDO_HDR_LEN-1);Index++)
	{
		CheckSum += CdoHdr[Index];
	}

	/* Invert checksum */
	CheckSum ^= 0xFFFFFFFFU;

	if (CheckSum != CdoHdr[Index])
	{
		XPlmi_Printf(DEBUG_GENERAL,
				"Config Object Checksum Failed\n\r");
		Status = XST_FAILURE;
		goto END;
	}else{
		Status = XST_SUCCESS;
	}

	XPlmi_Printf(DEBUG_INFO,
		"Config Object Version 0x%08x\n\r", CdoHdr[2]);
	XPlmi_Printf(DEBUG_INFO,
		"Length 0x%08x\n\r", CdoHdr[3]);
END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief Initialize the CDO pointer structure
 *
 * @param Pointer to the CDO structure
 *
 * @return None
 *
 *****************************************************************************/
void XPlmi_InitCdo(XPlmiCdo *CdoPtr)
{
	/** Initialize the CDO structure variables */
	CdoPtr->CopiedCmdLen = 0U;
	CdoPtr->CmdState = 0U;
	CdoPtr->BufLen = 0U;
	CdoPtr->CdoLen = 0U;
	CdoPtr->ProcessedCdoLen = 0U;

	/** Initialize the CDO buffer user params */
	CdoPtr->CmdEndDetected = FALSE;
	CdoPtr->Cdo1stChunk = TRUE;
}

/*****************************************************************************/
/**
 * @brief This function process the CDO file
 *
 * @param CdoPtr Pointer to the CDO structure
 *
 * @return XST_SUCCESS in case of success
 *
 *****************************************************************************/
int XPlmi_ProcessCdo(XPlmiCdo *CdoPtr)
{
	int Status = XST_SUCCESS;
	XPlmi_Cmd Cmd = CdoPtr->Cmd;
	u32 Size = 0U;
	u32 CopiedCmdLen = 0U;
	u32 *BufPtr = CdoPtr->BufPtr;
	u32 BufLen = CdoPtr->BufLen;

	/** verify the header for the first chunk of CDO */
	if (CdoPtr->Cdo1stChunk == TRUE)
	{
		Status = XPlmi_CdoVerifyHeader(CdoPtr);
		if (Status != XST_SUCCESS)
		{
			goto END;
		}
		CdoPtr->Cdo1stChunk = FALSE;
		CdoPtr->CdoLen = BufPtr[3];

		BufPtr += XPLMI_CDO_HDR_LEN;
		BufLen -= XPLMI_CDO_HDR_LEN;
		CdoPtr->BufLen -= XPLMI_CDO_HDR_LEN;
	}

	/** Check if BufLen is greater than CdoLen */
	if ((BufLen + CdoPtr->ProcessedCdoLen) > (CdoPtr->CdoLen))
	{
		BufLen = CdoPtr->CdoLen - CdoPtr->ProcessedCdoLen;
	}

	/**
	 * Incase CmdEnd is detected in previous iteration,
	 * it just returns
	 */
	if (CdoPtr->CmdEndDetected == TRUE)
	{
		goto END;
	}

	/**
	 * Check if cmd data is copied
	 * partially during the last iteration
	 */
	if (CdoPtr->CopiedCmdLen != 0)
	{
		CopiedCmdLen = CdoPtr->CopiedCmdLen;
		/** Copy the remaining cmd data */
		if (CopiedCmdLen == 1U) {
			/**
			 * To know the size, we need 2nd argument if
			 * length is greater than 255.
			 * Copy the 2nd argument to tempbuf to get the
			 * size correctly
			 */
			CdoPtr->TempCmdBuf[1] = BufPtr[0];
			CdoPtr->CopiedCmdLen++;
		}

		/** if size is greater than tempbuf, copy only tempbuf size */
		Size = XPlmi_CmdSize(CdoPtr->TempCmdBuf, CopiedCmdLen);
		if (Size > XPLMI_CMD_LEN_TEMPBUF) {
			Size = XPLMI_CMD_LEN_TEMPBUF;
		}
		memcpy(CdoPtr->TempCmdBuf + CdoPtr->CopiedCmdLen,
		       BufPtr, Size - CdoPtr->CopiedCmdLen);

		BufPtr = CdoPtr->TempCmdBuf;
		BufLen = Size;
		CdoPtr->CopiedCmdLen = 0U;
	}

	/** Execute the commands in the Cdo Buffer */
	while (BufLen > 0U)
	{
		/** Check if cmd has to be resumed */
		if (CdoPtr->CmdState == XPLMI_CMD_STATE_RESUME)
		{
			/** Update the Payload buffer and length */
			Cmd.PayloadLen = BufLen;
			Cmd.Payload = BufPtr;
			Status = XPlmi_CmdResume(&Cmd);
			if (Status != XST_SUCCESS)
			{
				goto END;
			}
			CdoPtr->CmdState = 0U;
		} else {
			/**
			 * Break if CMD says END of commands,
			 * irrespective of the CDO length
			 */
			if (BufPtr[0] == XPLMI_CMD_END)
			{
				XPlmi_Printf(DEBUG_DETAILED,
					"CMD END detected \n\r");
				CdoPtr->CmdEndDetected = TRUE;
				Status = XST_SUCCESS;
				break;
			}

			Size = XPlmi_CmdSize(BufPtr, BufLen);
			Cmd.Len = Size;

			/**
			 * Check if Cmd payload is less than buffer size,
			 * then copy to temparary buffer
			 */
			if ((Size > BufLen) && (BufLen < XPLMI_CMD_LEN_TEMPBUF))
			{
				/** Copy Cmd to temparary buffer */
				memcpy(CdoPtr->TempCmdBuf, BufPtr, BufLen);
				CdoPtr->CopiedCmdLen = BufLen;
				break;
			}

			/**
			 * if size is greater than tempbuf, execute partially
			 * and resume the cmd in next iteration
			 */
			if (Size > BufLen) {
				Size = XPLMI_CMD_LEN_TEMPBUF;
				CdoPtr->CmdState = XPLMI_CMD_STATE_RESUME;
			}

			/** Execute the command */
			XPlmi_SetupCmd(&Cmd, BufPtr, Size);
			Status = XPlmi_CmdExecute(&Cmd);
			if (Status != XST_SUCCESS)
			{
				break;
			}
		}

		/** Update the parameters for next iteration */
		if (CopiedCmdLen != 0U)
		{
			BufPtr = CdoPtr->BufPtr + (Size-CopiedCmdLen);
			BufLen = CdoPtr->BufLen - (Size-CopiedCmdLen);
			CopiedCmdLen = 0U;
		} else {
			BufPtr += Size;
			BufLen -= Size;
		}
	}

	CdoPtr->ProcessedCdoLen += CdoPtr->BufLen;
END:
	return Status;
}
