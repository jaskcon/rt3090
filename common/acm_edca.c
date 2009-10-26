/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  * 
 * it under the terms of the GNU General Public License as published by  * 
 * the Free Software Foundation; either version 2 of the License, or     * 
 * (at your option) any later version.                                   * 
 *                                                                       * 
 * This program is distributed in the hope that it will be useful,       * 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        * 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         * 
 * GNU General Public License for more details.                          * 
 *                                                                       * 
 * You should have received a copy of the GNU General Public License     * 
 * along with this program; if not, write to the                         * 
 * Free Software Foundation, Inc.,                                       * 
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             * 
 *                                                                       * 
 ***************************************************************************/

/****************************************************************************

	Abstract:

	All related WMM ACM EDCA function body.

***************************************************************************/

#define MODULE_WMM_ACM
#include "rt_config.h"

#ifdef WMM_ACM_SUPPORT

/* IEEE802.11E related include files */
#include "acm_extr.h" /* used for other modules */
#include "acm_comm.h" /* used for edca/wmm */
#include "acm_edca.h" /* used for edca/wmm */



/* ----- EDCA External Variable (only used in ACM module) ----- */
extern UCHAR gTCLAS_Elm_Len[];

#ifdef ACM_MEMORY_TEST
extern UINT32 gACM_MEM_Alloc_Num;
extern UINT32 gACM_MEM_Free_Num;
#endif // ACM_MEMORY_TEST //


/* ----- EDCA Private Variable ----- */
/* AC0 (BE), AC1 (BK), AC2 (VI), AC3 (VO); AC3 > AC2 > AC0 > AC1 */
/* DSCP = (DSCP >> 3) & 0x07 */
UCHAR gEDCA_UP_AC[8] = { 0, 1, 1, 0, 2, 2, 3, 3 };

/* AC0 vs. UP0, AC1 vs. UP1, AC2 vs. UP4, AC3 vs. UP6 */
UCHAR gEDCA_AC_UP[4] = { 0, 1, 4, 6 };

/* DSCP(0) -> Priority 0 (AC_BE)
   DSCP(1) -> Priority 1 (AC_BK)
   DSCP(2) -> Priority 2 (AC_BK)
   DSCP(3) -> Priority 3 (AC_BE)
   DSCP(4) -> Priority 4 (AC_VI)
   DSCP(5) -> Priority 5 (AC_VI)
   DSCP(6) -> Priority 6 (AC_VO)
   DSCP(7) -> Priority 7 (AC_VO) */
UCHAR gEDCA_UP_DSCP[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };





/* ====================== Public Function ============================= */

/*
========================================================================
Routine Description:
	Change current EDCA Information.

Arguments:
	pAd				- WLAN control block pointer
	CpNu			- the numerator of Contention Period,
						if 0, no any update for CpNu
	CpDe			- the denominator of Contention Period
						if 0, no any update for CpDe
	BeNu			- the numerator of Best Effort percentage,
						if 0, no any update for BeNu
	BeDe			- the denominator of Best Effort percentage
						if 0, no any update for BeDe

Return Value:
	ACM_RTN_OK		- change ok
	ACM_RTN_FAIL	- change fail

Note:
	1. CpNu/CpDe is the percentage of EDCA in 1 second.
	2. BeNu/BeDe is the percentage of Best Effort streams in 1 second.
	3. The function will not delete any stream if residunt
		bandwidth is not enough for (CpNu/CpDe)*SI or (BeNu/BeDe).
	4. New (CpNu/CpDe) or (BeNu/BeDe) will be valid after bandwidth is enough.
	5. If the old ACM is enabled and the new ACM is disabled,
		the function will not deleted these streams use the AC.
========================================================================
*/
ACM_FUNC_STATUS ACM_EDCA_InfomationChange(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC		pAd,
	ACM_PARAM_IN	UINT16					CpNu,
	ACM_PARAM_IN	UINT16					CpDe,
	ACM_PARAM_IN	UINT16					BeNu,
	ACM_PARAM_IN	UINT16					BeDe)
{
	ACM_CTRL_PARAM *pEdcaParam;
	ULONG SplFlags;


	/* change EDCA parameters */
	ACM_TSPEC_SEM_LOCK_CHK_RTN(pAd, SplFlags, LabelSemErr, ACM_RTN_FAIL);

	/* copy new settings to new parameters */
	pEdcaParam = &(ACMR_CB->EdcaCtrlParam);

	if ((CpNu > 0) && (CpDe > 0))
	{
		pEdcaParam->CP_MinNu = CpNu;
		pEdcaParam->CP_MinDe = CpDe;
	} /* End of if */

	if ((BeNu > 0) && (BeDe > 0))
	{
		pEdcaParam->BEK_MinNu = BeNu;
		pEdcaParam->BEK_MinDe = BeDe;
	} /* End of if */

	ACM_TSPEC_SEM_UNLOCK(pAd, LabelSemErr);

	/* delete all streams */
	ACMP_TC_DeleteAll(pAd);
	return ACM_RTN_OK;

LabelSemErr:
	return ACM_RTN_FAIL;
} /* End of ACM_EDCA_InfomationChange */


/*
========================================================================
Routine Description:
	Check whether the element is WME Information.

Arguments:
	*pElm			- the element
	SubType			- the sub type

Return Value:
	ACM_RTN_OK		- check ok
	ACM_RTN_FAIL	- check fail

Note:
	No semaphore protection is needed.
========================================================================
*/
ACM_FUNC_STATUS ACM_WME_ELM_Check(
	ACM_PARAM_IN	UCHAR				*pElm,
	ACM_PARAM_IN	UCHAR				SubType)
{
	UCHAR ElmLen[3] = { ACM_ELM_WME_INFO_LEN,
						ACM_ELM_WME_PARAM_LEN,
						ACM_ELM_WME_TSPEC_LEN };


	if ((pElm[0] == ACM_ELM_WME_ID) &&
		(pElm[1] == ElmLen[SubType]) &&
		(pElm[2] == ACM_WME_OUI_0) &&
		(pElm[3] == ACM_WME_OUI_1) &&
		(pElm[4] == ACM_WME_OUI_2) &&
		(pElm[5] == ACM_WME_OUI_TYPE) &&
		(pElm[6] == SubType) &&
		(pElm[7] == ACM_WME_OUI_VERSION))
	{
		return ACM_RTN_OK;
	} /* End of if */

	return ACM_RTN_FAIL;
} /* End of ACM_WME_ELM_Check */


#ifdef CONFIG_STA_SUPPORT
/*
========================================================================
Routine Description:
	Check a WME TSPEC element in a buffer.

Arguments:
	*pBuffer		- the buffer
	*pTid			- the TID of the TSPEC element
	*pMediumTime	- the medium time of the TSPEC element

Return Value:
	TRUE			- TSPEC element
	FALSE			- not TSPEC element

Note:
========================================================================
*/
BOOLEAN ACMP_WME_TSPEC_ElementCheck(
	ACM_PARAM_IN	UCHAR					*pBuffer,
	ACM_PARAM_OUT	UINT32					*pTid,
	ACM_PARAM_OUT	UINT32					*pMediumTime)
{
	ACM_ELM_WME_TSPEC *pElmTspec;


	if (pBuffer == NULL)
		return FALSE;
	/* End of if */

	pElmTspec = (ACM_ELM_WME_TSPEC *)pBuffer;

	if ((pElmTspec->ElementId != ACM_ELM_WME_ID) ||
		(pElmTspec->Length != ACM_ELM_WME_TSPEC_LEN) ||
		(pElmTspec->OUI[0] != ACM_WME_OUI_0) ||
		(pElmTspec->OUI[1] != ACM_WME_OUI_1) ||
		(pElmTspec->OUI[2] != ACM_WME_OUI_2) ||
		(pElmTspec->OUI_Type != ACM_WME_OUI_TYPE) ||
		(pElmTspec->OUI_SubType != ACM_WME_OUI_SUBTYPE_TSPEC) ||
		(pElmTspec->Version != ACM_WME_OUI_VERSION))
	{
		return FALSE;
	} /* End of if */

	*pTid = (UINT32)pElmTspec->Tspec.TsInfo.TID;
	*pMediumTime = (UINT32)pElmTspec->Tspec.MediumTime;
	return TRUE;
} /* End of ACMP_WME_TSPEC_ElementCheck */
#endif // CONFIG_STA_SUPPORT //


#ifdef CONFIG_STA_SUPPORT_SIM
/*
========================================================================
Routine Description:
	Fill a TSPEC element to a buffer.

Arguments:
	pAd				- WLAN control block pointer
	*pBuffer		- the buffer
	*pTspec11e		- the current TSPEC

Return Value:
	filled element length

Note:
========================================================================
*/
UINT32 ACMP_WME_TSPEC_ElementFill(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC		pAd,
	ACM_PARAM_IN	UCHAR					*pBuffer,
	ACM_PARAM_IN	ACM_TSPEC				*pTspec11e)
{
	ACM_ELM_WME_TSPEC *pElmTspec;
	ACM_WME_TSPEC *pTspecWme;
	ACM_WME_TS_INFO *pInfo;


	/* sanity check */
	if ((pBuffer == NULL) || (pTspec11e == NULL))
		return 0;
	/* End of if */

	/* TSPEC element */
	pElmTspec = (ACM_ELM_WME_TSPEC *)pBuffer;
	pElmTspec->ElementId = ACM_ELM_WME_ID;
	pElmTspec->Length = ACM_ELM_WME_TSPEC_LEN;

	/* init OUI field */
	pElmTspec->OUI[0] = ACM_WME_OUI_0;
	pElmTspec->OUI[1] = ACM_WME_OUI_1;
	pElmTspec->OUI[2] = ACM_WME_OUI_2;
	pElmTspec->OUI_Type = ACM_WME_OUI_TYPE;
	pElmTspec->OUI_SubType = ACM_WME_OUI_SUBTYPE_TSPEC;
	pElmTspec->Version = ACM_WME_OUI_VERSION;

	/* init TS Info field */
	pTspecWme = &pElmTspec->Tspec;
	ACMR_MEM_ZERO(pTspecWme, sizeof(ACM_WME_TSPEC));
	pInfo = &pElmTspec->Tspec.TsInfo;

	pInfo->TID = pTspec11e->TsInfo.TSID;
	pInfo->Direction = pTspec11e->TsInfo.Direction;
	pInfo->UP = pTspec11e->TsInfo.UP;
	pInfo->PSB = pTspec11e->TsInfo.APSD;
	pInfo->One = 1; /* always 1 */

#ifdef ACM_CC_FUNC_11N
	pInfo->Reserved2 = pTspec11e->TsInfo.AckPolicy;
#endif // ACM_CC_FUNC_11N //

	/* init TSPEC parameters */
	pTspecWme->NominalMsduSize = pTspec11e->NominalMsduSize;
	pTspecWme->MaxMsduSize = pTspec11e->MaxMsduSize;
	pTspecWme->MinServInt = pTspec11e->MinServInt;
	pTspecWme->MaxServInt = pTspec11e->MaxServInt;
	pTspecWme->InactivityInt = pTspec11e->InactivityInt;
	pTspecWme->SuspensionInt = pTspec11e->SuspensionInt;
	pTspecWme->ServiceStartTime = pTspec11e->ServiceStartTime;
	pTspecWme->MinDataRate = pTspec11e->MinDataRate;
	pTspecWme->MeanDataRate = pTspec11e->MeanDataRate;
	pTspecWme->PeakDataRate = pTspec11e->PeakDataRate;
	pTspecWme->MaxBurstSize = pTspec11e->MaxBurstSize;
	pTspecWme->DelayBound = pTspec11e->DelayBound;
	pTspecWme->MinPhyRate = pTspec11e->MinPhyRate;
	pTspecWme->SurplusBandwidthAllowance = \
							pTspec11e->SurplusBandwidthAllowance;

	return (sizeof(ACM_ELM_WME_TSPEC));
} /* End of ACMP_WME_TSPEC_ElementFill */
#endif // CONFIG_STA_SUPPORT_SIM //




/* ====================== Private Function (EDCA) (AP) ====================== */

/*
========================================================================
Routine Description:
	Reclaim a EDCA used ACM time after a actived stream is deleted.

Arguments:
	pAd				- WLAN control block pointer
	*pStream		- the EDCA stream

Return Value:
	None

Note:
========================================================================
*/
STATIC VOID ACM_EDCA_AllocatedTimeReturn(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC		pAd,
	ACM_PARAM_IN	ACM_STREAM				*pStream)
{
#define ACM_LMR_TIME_DECREASE(time, value)	\
	if (time >= value) time -= value; else time = 0;
#define ACM_LMR_TIME_INCREASE(time, value)	\
	time += value;

	ACM_CTRL_PARAM *pEdca = &ACMR_CB->EdcaCtrlParam;
	ACM_TSPEC *pTspec = pStream->pTspec;
	UINT32 TimeUsed, AcId;
	UINT32 Direction;


	/* check if the stream is EDCA */
	if (pTspec->TsInfo.AccessPolicy != ACM_ACCESS_POLICY_EDCA)
		return;
	/* End of if */

	/* get AC ID */
	AcId = ACM_MR_EDCA_AC(pStream->UP);

	/* reclaim used time */
	TimeUsed = pTspec->MediumTime << 5; /* unit: microsecond */

	if (TimeUsed == 0)
		return;
	/* End of if */

	ACM_LMR_TIME_DECREASE(pEdca->AcmTotalTime, TimeUsed);
	ACM_LMR_TIME_DECREASE(pEdca->AcmAcTime[AcId], TimeUsed);

	Direction = pTspec->TsInfo.Direction;


	/* for main link or bidirectional link, we shall reset the CSR */
	if ((pStream->FlgOutLink == 1) ||
		(Direction == ACM_DIRECTION_BIDIREC_LINK))
	{
		ACM_LMR_TIME_DECREASE(pEdca->AcmOutTime[AcId], TimeUsed);

		/* modify CSR setting */
		ACM_ASIC_ACM_Reset(pAd, AcId, pEdca->AcmOutTime[AcId]);
	} /* End of if */

	/* update DATL time */
	if (pEdca->FlgDatl)
		ACM_DATL_Update(pAd, AcId, TimeUsed, 0, ACM_DATL_NO_BORROW, 0);
	/* End of if */

	/* update available ACM time */
	TimeUsed = pEdca->AcmTotalTime;
#ifdef ACM_CC_FUNC_MBSS
	TimeUsed += ACMR_CB->MbssTotalUsedTime;
#endif // ACM_CC_FUNC_MBSS //

} /* End of ACM_EDCA_AllocatedTimeReturn */




/*
========================================================================
Routine Description:
	Update new ACM medium time for EDCA mechanism.

Arguments:
	pAd				- WLAN control block pointer
	AcmAcId			- the AC for the stream (0 ~ 3)
	Direction		- the Direction of the stream
	UP				- the user priority
	AcmTimeNew		- new medium time of the stream (unit: microseconds)
	AcmTimeOld		- old medium time of the stream (unit: microseconds)
	DatlAcId		- the borrowed AC ID, 0 ~ 3
	DatlBandwidth	- the borrowed bandwidth from a AC (unit: microsecond)

Return Value:
	None

Note:
========================================================================
*/
STATIC VOID ACM_EDCA_Param_ACM_Update(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC	pAd,
	ACM_PARAM_IN	UINT32				AcmAcId,
	ACM_PARAM_IN	UCHAR				Direction,
	ACM_PARAM_IN	UCHAR				UP,
	ACM_PARAM_IN	UINT32				AcmTimeNew,
	ACM_PARAM_IN	UINT32				AcmTimeOld,
	ACM_PARAM_IN	UINT32				DatlAcId,
	ACM_PARAM_IN	UINT32				DatlBandwidth)
{
	ACM_CTRL_PARAM  *pEdcaParam;
	UINT32 TimeNewDn, TimeOldDn;
	UINT32 TimeNewUp, TimeOldUp;
	UINT32 TimeUsed;
	UINT32 AcId;


	/* init */
	pEdcaParam = &(ACMR_CB->EdcaCtrlParam);
	TimeNewDn = 0;
	TimeOldDn = 0;
	TimeNewUp = 0;
	TimeOldUp = 0;
	AcId = AcmAcId;

	switch(Direction)
	{
		case ACM_DIRECTION_UP_LINK: /* uplink */
		case ACM_DIRECTION_DIRECT_LINK:
			TimeNewUp = AcmTimeNew;
			TimeOldUp = AcmTimeOld;

			break;

		case ACM_DIRECTION_DOWN_LINK: /* dnlink */
			TimeNewDn = AcmTimeNew;
			TimeOldDn = AcmTimeOld;

#ifdef CONFIG_STA_SUPPORT
			/* for dnlink in QSTA, AcmAcId is not AC ID; it is minor link;
				so we need to re-get AC ID from UP of the dnlink */
			AcId = ACM_MR_EDCA_AC(UP);
#endif // CONFIG_STA_SUPPORT //
			break;

		case ACM_DIRECTION_BIDIREC_LINK: /* dnlink + uplink */
			TimeNewDn = TimeNewUp = AcmTimeNew;
			TimeOldDn = TimeOldUp = AcmTimeOld;
			break;
	} /* End of switch */

	if ((TimeNewDn == TimeOldDn) && (TimeNewUp == TimeOldUp))
		return; /* same time, do NOT need to update */
	/* End of if */


	/* accumulate new allocated time */
#ifdef CONFIG_STA_SUPPORT
	pEdcaParam->AcmTotalTime += TimeNewUp;
	pEdcaParam->AcmOutTime[AcId] += TimeNewUp;
	pEdcaParam->AcmAcTime[AcId] += TimeNewUp;
#endif // CONFIG_STA_SUPPORT //


	/* substract old medium time from total ACM time parameters */
#ifdef CONFIG_STA_SUPPORT
	if (pEdcaParam->AcmTotalTime >= TimeOldUp)
		pEdcaParam->AcmTotalTime -= TimeOldUp;
#endif // CONFIG_STA_SUPPORT //
	else
	{
		pEdcaParam->AcmTotalTime = 0; /* fatal error */

		ACMR_DEBUG(ACMR_DEBUG_ERR,
				("acm_err> Used total ACM time < stream medium time! "
				"EDCA_Param_ACM_Update()\n"));
	} /* End of if */

#ifdef CONFIG_STA_SUPPORT
	if (pEdcaParam->AcmOutTime[AcId] >= TimeOldUp)
		pEdcaParam->AcmOutTime[AcId] -= TimeOldUp;
#endif // CONFIG_STA_SUPPORT //
	else
	{
		pEdcaParam->AcmOutTime[AcId] = 0; /* fatal error */

		ACMR_DEBUG(ACMR_DEBUG_ERR,
				("acm_err> Used ACM time < stream medium time! "
				"EDCA_Param_ACM_Update()\n"));
	} /* End of if */


	/* update the ACM used time of the AC */
#ifdef CONFIG_STA_SUPPORT
	if (pEdcaParam->AcmAcTime[AcId] >= TimeOldUp)
		pEdcaParam->AcmAcTime[AcId] -= TimeOldUp;
#endif // CONFIG_STA_SUPPORT //
	else
	{
		pEdcaParam->AcmAcTime[AcId] = 0; /* fatal error */

		ACMR_DEBUG(ACMR_DEBUG_ERR,
				("acm_err> Used AC ACM time < stream medium time! "
				"EDCA_Param_ACM_Update()\n"));
	} /* End of if */


	/* update DATL time only in QAP */
	if (pEdcaParam->FlgDatl)
	{
		ACM_DATL_Update(pAd, AcId, AcmTimeOld, AcmTimeNew,
						DatlAcId, DatlBandwidth);
	} /* End of if */


	/* update available ACM time */
	TimeUsed = pEdcaParam->AcmTotalTime;
#ifdef ACM_CC_FUNC_MBSS
	TimeUsed += ACMR_CB->MbssTotalUsedTime;
#endif // ACM_CC_FUNC_MBSS //

} /* End of ACM_EDCA_Param_ACM_Update */




/* ====================== Private Function (WMM) =========================== */

#ifdef ACM_CC_FUNC_WMM

/*
========================================================================
Routine Description:
	Translate 11e status code to WME status code.

Arguments:
	StatusCode		- 11e status code

Return Value:
	WME status code

Note:
	Only 3 status code for WMM ACM.

	WLAN_STATUS_CODE_WME_INVALID_PARAM	- invalid TSPEC parameters
	WLAN_STATUS_CODE_WME_ACM_ACCEPTED	- accept
	WLAN_STATUS_CODE_WME_REFUSED		- refuse due to insufficient BW
========================================================================
*/
STATIC UCHAR ACM_11E_WMM_StatusTranslate(
	ACM_PARAM_IN	UCHAR		StatusCode)
{
	ACMR_DEBUG(ACMR_DEBUG_TRACE,
				("acm_msg> 11e status code = %d\n", StatusCode));

	if (StatusCode == ACM_STATUS_CODE_INVALID_PARAMETERS)
		return WLAN_STATUS_CODE_WME_INVALID_PARAM;
	/* End of if */

	if (StatusCode == ACM_STATUS_CODE_SUCCESS)
		return WLAN_STATUS_CODE_WME_ACM_ACCEPTED;
	/* End of if */

	return WLAN_STATUS_CODE_WME_REFUSED;
} /* End of ACM_11E_WMM_StatusTranslate */



/*
========================================================================
Routine Description:
	Translate WME TSPEC & TCLAS to 11e TSPEC & TCLAS.

Arguments:
	*pPktElm			- the TSPEC related element in the packet buffer
	BodyLen				- the action frame length
	*pETspec			- the 11e TSPEC
	*pTclas				- the 11e TCLAS
	*pTclasNum			- the number of TCLAS
	*pTclasProcessing	- the TCLAS PROCESSING value

Return Value:
	ACM_RTN_OK			- translate ok
	ACM_RTN_FAIL		- translate fail

Note:
	Internally we use 11e TSPEC, not WMM TSPEC.
========================================================================
*/
STATIC ACM_FUNC_STATUS ACM_WME_11E_TSPEC_TCLAS_Translate(
	ACM_PARAM_IN	UCHAR					*pPktElm,
	ACM_PARAM_IN	UINT32					BodyLen,
	ACM_PARAM_IN	ACM_TSPEC				*pETspec,
	ACM_PARAM_IN	ACM_TCLAS				**pTclas,
	ACM_PARAM_IN	UINT32					*pTclasNum,
	ACM_PARAM_IN	UCHAR					*pTclasProcessing)
{
	ACM_WME_TSPEC *pTspec;
	ACM_ELM_WME_TCLAS *pElmTclas;
	ACM_ELM_WME_TCLAS_PROCESSING *pElmTclasProcessing;
	UCHAR *pElm;
	UCHAR ElmID, ElmLen, ElmSubID;
	UCHAR TclasType;
	UINT32 IdTclasNum;


	/* init */
	pTspec = NULL;

	for(IdTclasNum=0; IdTclasNum<ACM_TSPEC_TCLAS_MAX_NUM; IdTclasNum++)
		pTclas[IdTclasNum] = NULL;
	/* End of for */

	pElm = (UCHAR *)pPktElm;

	*pTclasNum = 0;
	*pTclasProcessing = ACM_TCLAS_PROCESSING_NOT_EXIST;

	BodyLen -= 4; /* skip Category, action, DialogToken, & StatusCode */

	/* parsing TSPEC, TCLASS, & TCLASS Processing elements */
	while(BodyLen > 0)
	{
		ElmID = *pElm;
		ElmLen = *(pElm+1);

		if (BodyLen < (UINT32)(ACM_ELM_ID_LEN_SIZE+ElmLen))
		{
			/* fatal error, packet size is not enough */
			ACMR_DEBUG(ACMR_DEBUG_ERR,
						("acm_err> packet length %d is too small %d! "
						"WME_11E_TSPEC_TCLAS_Translate()\n",
						BodyLen, (ACM_ELM_ID_LEN_SIZE+ElmLen)));
			goto label_parsing_err;
		} /* End of if */

		/* not check *(pElm+1) = element length and
			not check *(pElm+6) = WMM sub element ID */
		if ((ElmID != ACM_ELM_WME_ID) ||
			(*(pElm+2) != ACM_WME_OUI_0) ||
			(*(pElm+3) != ACM_WME_OUI_1) ||
			(*(pElm+4) != ACM_WME_OUI_2) ||
			(*(pElm+5) != ACM_WME_OUI_TYPE) ||
			(*(pElm+7) != ACM_WME_OUI_VERSION))
		{
			/* not WMM element so check next element */
			pElm += (ACM_ELM_ID_LEN_SIZE+ElmLen);
			BodyLen -= (ACM_ELM_ID_LEN_SIZE+ElmLen);
			continue;
		} /* End of if */

		ElmSubID = *(pElm+ACM_WME_OUI_ID_OFFSET);

		switch(ElmSubID)
		{
			case ACM_WME_OUI_SUBTYPE_TSPEC: /* TSPEC element */
				ACMR_DEBUG(ACMR_DEBUG_TRACE,
							("acm_msg> find a WMM TSPEC element! "
							"WME_11E_TSPEC_TCLAS_Translate()\n"));

				pTspec = &((ACM_ELM_WME_TSPEC *)pElm)->Tspec;

				if (ACM_WME_11E_TSPEC_Translate(pTspec,
												pETspec) != ACM_RTN_OK)
				{
					goto label_parsing_err;
				} /* End of if */
				break;

			case ACM_WSM_OUI_SUBTYPE_TCLAS: /* TCLASS element */
				ACMR_DEBUG(ACMR_DEBUG_TRACE,
							("acm_msg> find a WMM TCLAS element! "
							"WME_11E_TSPEC_TCLAS_Translate()\n"));

				/* sanity check for TCLAS number & element length */
				if ((*pTclasNum) >= ACM_TCLAS_MAX_NUM)
					goto label_parsing_err;
				/* End of if */

				/* skip element id/len, OUI header, user priority */
				TclasType = *(pElm+2+ACM_WME_OUI_HDR_LEN+1);

				switch(TclasType)
				{
					case ACM_TCLAS_TYPE_ETHERNET:
						if (ElmLen != ACM_TCLAS_TYPE_WME_ETHERNET_LEN)
							goto label_parsing_err;
						/* End of if */
						break;

					case ACM_TCLAS_TYPE_IP_V4:
						if (ElmLen != ACM_TCLAS_TYPE_WME_IP_V4_LEN)
							goto label_parsing_err;
						/* End of if */
						break;

					case ACM_TCLAS_TYPE_8021DQ:
						if (ElmLen != ACM_TCLAS_TYPE_WME_8021DQ_LEN)
							goto label_parsing_err;
						/* End of if */
						break;

					default:
						goto label_parsing_err;
				} /* End of switch */

				pElmTclas = (ACM_ELM_WME_TCLAS *)pElm;
				pTclas[(*pTclasNum)++] = &pElmTclas->Tclas;
				break;

			case ACM_WSM_OUI_SUBTYPE_TCLAS_PROCESSING: /* TCLASS Processing */
				if (ElmLen != ACM_ELM_WME_TCLAS_PROCESSING_LEN)
					goto label_parsing_err;
				/* End of if */

				ACMR_DEBUG(ACMR_DEBUG_TRACE,
							("acm_msg> find a WMM TCLAS PROCESSING element! "
							"WME_11E_TSPEC_TCLAS_Translate()\n"));

				pElmTclasProcessing = (ACM_ELM_WME_TCLAS_PROCESSING *)pElm;
				*pTclasProcessing = pElmTclasProcessing->Processing;
				break;
		} /* End of switch */

		/* check next element */
		pElm += (2+ElmLen);
		BodyLen -= (2+ElmLen);
	} /* End of while */

	return ACM_RTN_OK;

label_parsing_err:
	return ACM_RTN_FAIL;
} /* End of ACM_WME_11E_TSPEC_TCLAS_Translate */


/*
========================================================================
Routine Description:
	Translate WME TSPEC to 11e TSPEC.

Arguments:
	*pWTspec		- the 'W'ME TSPEC
	*pETspec		- the 11'e' TSPEC

Return Value:
	ACM_RTN_OK		- translate ok
	ACM_RTN_FAIL	- translate fail

Note:
========================================================================
*/
STATIC ACM_FUNC_STATUS ACM_WME_11E_TSPEC_Translate(
	ACM_PARAM_IN	ACM_WME_TSPEC			*pWTspec,
	ACM_PARAM_IN	ACM_TSPEC				*pETspec)
{
	ACM_TS_INFO *pInfo;


	/* init */
	pInfo = &pETspec->TsInfo;

	/* translate WMM TSPEC to 11e TSPEC */
	ACMR_MEM_ZERO((UCHAR *)pETspec, sizeof(ACM_TSPEC));

	/* init TS Info field */
	pInfo->TrafficType = pWTspec->TsInfo.Reserved4;
	pInfo->TSID = pWTspec->TsInfo.TID;

	pInfo->Direction = pWTspec->TsInfo.Direction;
	pInfo->AccessPolicy = pWTspec->TsInfo.One;
	pInfo->Aggregation = pWTspec->TsInfo.Zero1;
	pInfo->APSD = pWTspec->TsInfo.PSB;
	pInfo->UP = pWTspec->TsInfo.UP;
	pInfo->AckPolicy = pWTspec->TsInfo.Reserved2;

	/* in WMM ACM TG, we need to check bit16 ~ 23 and bit8 == 0,
		and we will not	use schedule field, so we set (bit16 ~ 23 | bit8)
		to the field */
	pInfo->Schedule = pWTspec->TsInfo.Reserved1 |
						pWTspec->TsInfo.Reserved3;

	/* init TSPEC parameters */
	pETspec->NominalMsduSize = pWTspec->NominalMsduSize;
	pETspec->MaxMsduSize = pWTspec->MaxMsduSize;
	pETspec->MinServInt = pWTspec->MinServInt;
	pETspec->MaxServInt = pWTspec->MaxServInt;
	if (pWTspec->InactivityInt == 0)
	{
		/* can not be 0 so use default timeout */
		pETspec->InactivityInt = ACM_WME_TSPEC_INACTIVITY_DEFAULT;
	}
	else
		pETspec->InactivityInt = pWTspec->InactivityInt;
	/* End of if */

	pETspec->SuspensionInt = pWTspec->SuspensionInt;
	pETspec->ServiceStartTime = pWTspec->ServiceStartTime;
	pETspec->MinDataRate = pWTspec->MinDataRate;
	pETspec->MeanDataRate = pWTspec->MeanDataRate;
	pETspec->PeakDataRate = pWTspec->PeakDataRate;

	/* if you want to issue NULL TSPEC, Min = Mean = Peak = 0 */
	if (pETspec->MeanDataRate == 0)
	{
		if (pETspec->PeakDataRate != 0)
			pETspec->MeanDataRate = pETspec->PeakDataRate;
		else
		{
			if (pETspec->MinDataRate != 0)
				pETspec->MeanDataRate = pETspec->MinDataRate;
			/* End of if */
		} /* End of if */
	} /* End of if */

	pETspec->MaxBurstSize = pWTspec->MaxBurstSize;
	pETspec->DelayBound = pWTspec->DelayBound;
	pETspec->MinPhyRate = pWTspec->MinPhyRate;
	pETspec->SurplusBandwidthAllowance = \
										pWTspec->SurplusBandwidthAllowance;
	pETspec->MediumTime = pWTspec->MediumTime;
	return ACM_RTN_OK;
} /* End of ACM_WME_11E_TSPEC_Translate */


/*
========================================================================
Routine Description:
	Make a WME action frame body.

Arguments:
	pAd				- WLAN control block pointer
	*pStream		- the stream
	*pPkt			- the frame body pointer
	Action			- action
	StatusCode		- status code, used when action = response

Return Value:
	ACM_RTN_OK		- insert ok
	ACM_RTN_FAIL	- insert fail

Note:
========================================================================
*/
STATIC UINT32 ACM_WME_ActionFrameBodyMake(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC	pAd,
	ACM_PARAM_IN	ACM_STREAM			*pStream,
	ACM_PARAM_IN	UCHAR				*pPkt,
	ACM_PARAM_IN	UCHAR				Action,
	ACM_PARAM_IN	UCHAR				StatusCode)
{
	ACM_WME_NOT_FRAME *pFrameBody;
	ACM_ELM_WME_TSPEC *pElmTspec;
	ACM_ELM_WME_TCLAS_PROCESSING *pElmTspecProcessing;
	ACM_WME_TS_INFO *pInfo;
	ACM_WME_TSPEC *pTspec;
	UCHAR *pElmTclas;
	UINT32 BodyLen, Len;
	UINT32 IdTclasNum;


	/* sanity check for type */
	if (Action > ACM_ACTION_WME_TEAR_DOWN)
		return 0;
	/* End of if */

	/* init frame body */
	pFrameBody = (ACM_WME_NOT_FRAME *)pPkt;
	pFrameBody->Category = ACM_CATEGORY_WME;
	pFrameBody->Action   = Action;

	if (Action != ACM_ACTION_WME_TEAR_DOWN)
		pFrameBody->DialogToken = pStream->DialogToken;
	else
		pFrameBody->DialogToken = 0;
	/* End of if */

	pFrameBody->StatusCode = StatusCode;
	BodyLen = 4;

	/* TSPEC element */
	pElmTspec = &pFrameBody->ElmTspec;
	pElmTspec->ElementId = ACM_ELM_WME_ID;
	pElmTspec->Length = ACM_ELM_WME_TSPEC_LEN;

	/* init OUI field */
	pElmTspec->OUI[0] = ACM_WME_OUI_0;
	pElmTspec->OUI[1] = ACM_WME_OUI_1;
	pElmTspec->OUI[2] = ACM_WME_OUI_2;
	pElmTspec->OUI_Type = ACM_WME_OUI_TYPE;
	pElmTspec->OUI_SubType = ACM_WME_OUI_SUBTYPE_TSPEC;
	pElmTspec->Version = ACM_WME_OUI_VERSION;

	/* init TS Info field */
	pTspec = &pFrameBody->ElmTspec.Tspec;
	ACMR_MEM_ZERO(pTspec, sizeof(ACM_WME_TSPEC));
	pInfo = &pFrameBody->ElmTspec.Tspec.TsInfo;

	pInfo->TID = pStream->pTspec->TsInfo.TSID;
	pInfo->Direction = pStream->pTspec->TsInfo.Direction;
	pInfo->UP = pStream->pTspec->TsInfo.UP;
	pInfo->PSB = pStream->pTspec->TsInfo.APSD;
	pInfo->One = 1; /* always 1 */

#ifdef ACM_CC_FUNC_11N
	pInfo->Reserved2 = pStream->pTspec->TsInfo.AckPolicy;
#endif // ACM_CC_FUNC_11N //

	/* init TSPEC parameters */
	pTspec->NominalMsduSize = pStream->pTspec->NominalMsduSize;
	pTspec->MaxMsduSize = pStream->pTspec->MaxMsduSize;
	pTspec->MinServInt = pStream->pTspec->MinServInt;
	pTspec->MaxServInt = pStream->pTspec->MaxServInt;
	pTspec->InactivityInt = pStream->pTspec->InactivityInt;
	pTspec->SuspensionInt = pStream->pTspec->SuspensionInt;
	pTspec->ServiceStartTime = pStream->pTspec->ServiceStartTime;
	pTspec->MinDataRate = pStream->pTspec->MinDataRate;
	pTspec->MeanDataRate = pStream->pTspec->MeanDataRate;
	pTspec->PeakDataRate = pStream->pTspec->PeakDataRate;
	pTspec->MaxBurstSize = pStream->pTspec->MaxBurstSize;
	pTspec->DelayBound = pStream->pTspec->DelayBound;
	pTspec->MinPhyRate = pStream->pTspec->MinPhyRate;
	pTspec->SurplusBandwidthAllowance = \
								pStream->pTspec->SurplusBandwidthAllowance;

	if (pTspec->TsInfo.Direction != ACM_DIRECTION_DOWN_LINK)
	{
		/* we need to fill medium time if the link is not downlink-only */
		pTspec->MediumTime = pStream->pTspec->MediumTime;
	} /* End of if */

	BodyLen += (ACM_ELM_ID_LEN_SIZE+pElmTspec->Length);

	/* TCLASS element */
	pElmTclas = pFrameBody->Tclas;

	for(IdTclasNum=0; IdTclasNum<ACM_TSPEC_TCLAS_MAX_NUM; IdTclasNum++)
	{
		if (pStream->pTclas[IdTclasNum] != NULL)
		{
			*pElmTclas++ = ACM_ELM_WME_ID;
			Len = ACM_TCLAS_LEN_GET(pStream->pTclas[IdTclasNum]->ClassifierType);
			*pElmTclas++ = Len;

			*pElmTclas++ = ACM_WME_OUI_0;
			*pElmTclas++ = ACM_WME_OUI_1;
			*pElmTclas++ = ACM_WME_OUI_2;
			*pElmTclas++ = ACM_WME_OUI_TYPE;
			*pElmTclas++ = ACM_WSM_OUI_SUBTYPE_TCLAS;
			*pElmTclas++ = ACM_WME_OUI_VERSION;

			ACMR_MEM_COPY(pElmTclas,
						pStream->pTclas[IdTclasNum],
						Len-ACM_WME_OUI_HDR_LEN);

			pElmTclas += (Len-ACM_WME_OUI_HDR_LEN);

			BodyLen += (ACM_ELM_ID_LEN_SIZE+Len);
			continue; /* check next TCLAS */
		} /* End of if */

		break; /* no more TCLAS exists */
	} /* End of for */

	/* TCLASS Processing element */
	if (pStream->pTclas[0] != NULL)
	{
		/* TCLAS PROCESSING element exists only when at least one TCLAS element
			exists */
		if (pStream->TclasProcessing != ACM_TCLAS_PROCESSING_NOT_EXIST)
		{
			pElmTspecProcessing = (ACM_ELM_WME_TCLAS_PROCESSING *)pElmTclas;
			BodyLen += (ACM_ELM_ID_LEN_SIZE+ACM_ELM_WME_TCLAS_PROCESSING_LEN);

			pElmTspecProcessing->ElementId = ACM_ELM_WME_ID;
			pElmTspecProcessing->Length = ACM_ELM_WME_TCLAS_PROCESSING_LEN;

			pElmTspecProcessing->OUI[0] = ACM_WME_OUI_0;
			pElmTspecProcessing->OUI[1] = ACM_WME_OUI_1;
			pElmTspecProcessing->OUI[2] = ACM_WME_OUI_2;
			pElmTspecProcessing->OUI_Type = ACM_WME_OUI_TYPE;
			pElmTspecProcessing->OUI_SubType = ACM_WSM_OUI_SUBTYPE_TCLAS_PROCESSING;
			pElmTspecProcessing->Version = ACM_WME_OUI_VERSION;

			pElmTspecProcessing->Processing = pStream->TclasProcessing;
		} /* End of if */
	} /* End of if */

	return BodyLen;
} /* End of ACM_WME_ActionFrameBodyMake */


/*
========================================================================
Routine Description:
	Handle a WME action frame.

Arguments:
	pAd				- WLAN control block pointer
	*pCdb			- the source QSTA
	*pFrameBody		- the action frame body
	BodyLen			- the length of action frame body
	PhyRate			- the physical tx rate for the frame, bps
	Action			- Setup request, response, or teardown
	*pStatusCode	- response status code
	*pMediumTime	- the allowed medium time

Return Value:
	None

Note:
========================================================================
*/
STATIC VOID ACM_WME_ActionHandle(
	ACM_PARAM_IN	ACMR_PWLAN_STRUC	pAd,
	ACM_PARAM_IN	ACMR_STA_DB			*pCdb,
	ACM_PARAM_IN	UCHAR				*pFrameBody,
	ACM_PARAM_IN	UINT32				BodyLen,
	ACM_PARAM_IN	UINT32				PhyRate,
	ACM_PARAM_IN	UCHAR				Action,
	ACM_PARAM_OUT	UCHAR				*pStatusCode,
	ACM_PARAM_OUT	UINT16				*pMediumTime)
{
	ACM_WME_NOT_FRAME *pNotFrame;
	ACM_TCLAS *pTclas[ACM_TSPEC_TCLAS_MAX_NUM];
	ACM_TSPEC Tspec;
	UINT32 TclasNum;
	UCHAR TclasProcessing;
	UCHAR StatusCode;
	ACM_FUNC_STATUS RtnCode;


	/* init */
	pNotFrame = (ACM_WME_NOT_FRAME *)pFrameBody;
	TclasNum = 0;
	TclasProcessing = ACM_TCLAS_PROCESSING_NOT_EXIST;
	StatusCode = ACM_STATUS_CODE_SUCCESS;

	/* sanity check for input parameters */
	if (Action > ACM_ACTION_WME_TEAR_DOWN)
	{
		ACMR_DEBUG(ACMR_DEBUG_ERR,
					("acm_err> Error action type = %d! "
					"WME_ActionHandle()\n", Action));
		return;
	} /* End of if */

	if (ACM_WME_ELM_Check((UCHAR *)&pNotFrame->ElmTspec,
							ACM_WME_OUI_SUBTYPE_TSPEC) != ACM_RTN_OK)
	{
		ACMR_DEBUG(ACMR_DEBUG_TRACE,
					("acm_msg> Element check error! "
					"WME_ActionHandle()\n"));
		return; /* TSPEC element error */
	} /* End of if */

	if (BodyLen < ACM_NOT_FRAME_BODY_LEN)
	{
		ACMR_DEBUG(ACMR_DEBUG_TRACE,
					("acm_msg> Frame length is not enough! "
					"WME_ActionHandle()\n"));
		return; /* error! < minimum action frame length */
	} /* End of if */

	/* translate WME TSPEC to 11e TSPEC */
	if (ACM_WME_11E_TSPEC_TCLAS_Translate(
										(UCHAR *)&pNotFrame->ElmTspec,
										BodyLen,
										&Tspec,
										pTclas,
										&TclasNum,
										&TclasProcessing) != ACM_RTN_OK)
	{
		ACMR_DEBUG(ACMR_DEBUG_ERR,
					("acm_err> Translate TSPEC fail! "
					"WME_ActionHandle()\n"));
		return; /* translate fail */
	} /* End of if */

	/* handle it by action */
	switch(Action)
	{

#ifdef CONFIG_STA_SUPPORT
		case ACM_ACTION_WME_SETUP_RSP:
			RtnCode = ACM_TC_RspHandle(
							pAd, pCdb, pNotFrame->DialogToken,
							pNotFrame->StatusCode,
							&Tspec, NULL, &StatusCode);

			if (RtnCode != ACM_RTN_OK)
			{
				ACMR_DEBUG(ACMR_DEBUG_TRACE,
							("acm_msg> A WME Setup response is error %d! "
							"WME_ActionHandle()\n", RtnCode));
			} /* End of if */
			break;
#endif // CONFIG_STA_SUPPORT //

		case ACM_ACTION_WME_TEAR_DOWN:
			ACMR_DEBUG(ACMR_DEBUG_TRACE,
						("acm_msg> A WME Tear down is RCV! "
						"DEL the stream! WME_ActionHandle()\n"));

			ACM_TC_DestroyBy_TS_Info(
									pAd,
									ACMR_CLIENT_MAC(pCdb),
									&Tspec.TsInfo,
									ACMR_IS_AP_MODE);
			break;

		default:
			break;
	} /* End of switch */

	/* upadte status code */
	*pStatusCode = StatusCode;
} /* End of ACM_WME_ActionHandle */




/* ====================== Private Function (WMM) (AP) ====================== */


#endif // ACM_CC_FUNC_WMM //

#endif // WMM_ACM_SUPPORT //

/* End of acm_edca.c */
