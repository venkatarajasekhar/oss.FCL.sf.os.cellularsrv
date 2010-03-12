// Copyright (c) 2006-2009 Nokia Corporation and/or its subsidiary(-ies).
// All rights reserved.
// This component and the accompanying materials are made available
// under the terms of "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
//
// Initial Contributors:
// Nokia Corporation - initial contribution.
//
// Contributors:
//
// Description:
// PDP SubConnection Provider states/transitions
// 
//

/**
 @file
 @internalComponent
*/

#include <comms-infras/es_config.h>
#include <comms-infras/corescpractivities.h>
#include <comms-infras/ss_mcprnodemessages.h>
#include "PDPSCPRStates.h"
#include "PDPDeftSCPR.h"
#include "PDPParamMapper.h"
#include <comms-infras/ss_log.h>
#include <networking/ipaddrinfoparams.h>
#include <networking/mbmsparams.h>
#include <ip_subconparams.h>
#include <genericscprparams.h>
#include "pdpfsmnmspace.h"
#include "pdpdef.h"
#include <comms-infras/ss_nodemessages_flow.h>
#include <comms-infras/ss_nodemessages.h>
#include <comms-infras/ss_api_ext.h>
#include <elements/nm_messages_errorrecovery.h>
#include <comms-infras/sockmes.h> // for ioctl ipc
#include <nifvar_internal.h>
#include <elements/mm_activities.h>

using namespace Messages;
using namespace MeshMachine;
using namespace ESock;
using namespace NetStateMachine;
using namespace PDPSCprActivities;
using namespace SpudMan;
using namespace ConnectionServ;

#ifdef _DEBUG
_LIT (KPdpSCprPanic,"PpdScprPanic");
#endif

//-=========================================================
//
// States
//
//-=========================================================
namespace PDPSCprStates
{
//-=========================================================
//Util
//-=========================================================
DEFINE_SMELEMENT(TAwaitingPDPFSMMessage, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPDPFSMMessage::Accept()
    {
    return iContext.iMessage.IsMessage<TPDPFSMMessages::TPDPFSMMessage>();
    }

DEFINE_SMELEMENT(TNoTagOrError, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagOrError::TransitionTag()
    {
    ASSERT(iContext.iNodeActivity);
    TPDPFSMMessages::TPDPFSMMessage& msg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);
    if (msg.iValue2 != KErrNone)
        {
        iContext.iNodeActivity->SetError(msg.iValue2);
        return KErrorTag;
        }
    return KNoTag;
    }

DEFINE_SMELEMENT(TNoTagOrAlreadyStarted, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagOrAlreadyStarted::TransitionTag()
    {
    ASSERT(iContext.iMessage.IsMessage<TCFDataClient::TStart>());
    ASSERT(iContext.iNodeActivity);
    if (iContext.Node().iPDPFsmContextId != CPDPSubConnectionProvider::EInvalidContextId)
        {
        return CoreNetStates::KAlreadyStarted;
        }
    return KNoTag;
    }


TBool TAwaitingPDPFSMMessage::Accept(TInt aExtensionId)
    {
    TPDPFSMMessages::TPDPFSMMessage* pdpmsg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(&iContext.iMessage);
    if ( pdpmsg )
        {
        if (pdpmsg->iValue1 == aExtensionId)
            {
            return ETrue;
            }
        }
    return EFalse;
    }

DEFINE_SMELEMENT(TSendDataClientIdleIfNoSubconnsAndNoClients, NetStateMachine::MStateTransition, PDPSCprStates::TDefContext)
void TSendDataClientIdleIfNoSubconnsAndNoClients::DoL()
    {
	iContext.Node().SendDataClientIdleIfNoSubconnsAndNoClientsL();
	}

DEFINE_SMELEMENT(TNoTagOrSendErrorRecoveryRequestOrError, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagOrSendErrorRecoveryRequestOrError::TransitionTag()
	{
	ASSERT(iContext.iNodeActivity);
	TPDPFSMMessages::TPDPFSMMessage& msg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);
	if (msg.iValue2 == KErrUmtsMaxNumOfContextExceededByNetwork ||
	    msg.iValue2 == KErrUmtsMaxNumOfContextExceededByPhone)
		{
		return KSendErrorRecoveryRequest;
		}
	else if (msg.iValue2 != KErrNone)
		{
		iContext.iNodeActivity->SetError(msg.iValue2);
		return KErrorTag;
		}
	return KNoTag;
	}

DEFINE_SMELEMENT(TNoTagBackwardOrErrorTag, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagBackwardOrErrorTag::TransitionTag()
	{
	__ASSERT_DEBUG(iContext.iNodeActivity, User::Panic(KPdpSCprPanic, CorePanics::KPanicNoActivity));
	iContext.Node().SetContentionRequested(EFalse);
	if (iContext.iMessage.IsMessage<TEErrorRecovery::TErrorRecoveryResponse>() &&
		message_cast<TEErrorRecovery::TErrorRecoveryResponse>(iContext.iMessage).iErrResponse.iAction == TErrResponse::ERetry)
		{
		return KNoTag | NetStateMachine::EBackward;	
		}

		
	TEErrorRecovery::TErrorRecoveryResponse* recoveryMsg = message_cast<TEErrorRecovery::TErrorRecoveryResponse>(&iContext.iMessage);
	if (recoveryMsg)
		{
		iContext.iNodeActivity->SetError(recoveryMsg->iErrResponse.iError);
		}
		
		
	TEBase::TError* errMsg = message_cast<TEBase::TError>(&iContext.iMessage);
	if (errMsg)
		{
		iContext.iNodeActivity->SetError(errMsg->iValue);
		}
	return KErrorTag;
	}

DEFINE_SMELEMENT(TNoTagOrContentionTag, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagOrContentionTag::TransitionTag()
	{
	if (iContext.Node().ContentionRequested())
		{
		return KContentionTag;
		}
	return KNoTag;
	}



//-=========================================================
//Provisioning
//-=========================================================
DEFINE_SMELEMENT(TSelfInit, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSelfInit::DoL()
    {
    CPDPSubConnectionProvider &tNode = static_cast<CPDPSubConnectionProvider&>(iContext.Node());
    
    // if the FSM interface is null, this means that we're initializing a secondary context
    // as this code is common for both
    
    if (tNode.iPdpFsmInterface == NULL)
        {
        //Non-default SCPR running this code
        ASSERT(iContext.Node().ControlProvider());
        ESock::TDefaultSCPRFactoryQuery query(iContext.Node().ControlProvider()->RecipientId(), TSubConnOpen::EAttachToDefault );
        ESock::CSubConnectionProviderBase* defProvider = static_cast<ESock::CSubConnectionProviderBase*>(static_cast<ESock::CSubConnectionProviderFactoryBase&>(iContext.Node().Factory()).Find(query));
        ASSERT(defProvider);
        iContext.Node().iDefaultSCPR = static_cast<CPDPDefaultSubConnectionProvider*>(defProvider);
        ASSERT(iContext.Node().iDefaultSCPR != &iContext.Node()); //This transition is not expected to be executed for defaults.
        ASSERT(iContext.Node().iDefaultSCPR->iPdpFsmInterface != NULL);
        ASSERT(iContext.Node().iPdpFsmInterface == NULL);
        iContext.Node().iPdpFsmInterface = iContext.Node().iDefaultSCPR->iPdpFsmInterface;
        iContext.Node().iPdpFsmInterface->Open();
        }
    else
        {
        //default SCPR running this code (i.e. primary context)
        const CTSYProvision* tsyProvision =\
                static_cast<const CTSYProvision*>(tNode.AccessPointConfig().FindExtension(CTSYProvision::TypeId()));
        
        if (tsyProvision == NULL)
        	{
            // we do not have to set provision failure here
            // as it was set in the base class constructor
            // svg will show the leave error code though
        	User::Leave(KErrCorrupt);
        	}
        
        CGPRSProvision* gprsProvision = const_cast<CGPRSProvision*>(static_cast<const CGPRSProvision*>(
        	    tNode.AccessPointConfig().FindExtension(STypeId::CreateSTypeId(CGPRSProvision::EUid,CGPRSProvision::ETypeId))));
        
        if (gprsProvision == NULL)
            {
            tNode.iProvisionFailure = KErrCorrupt;
            User::Leave(KErrCorrupt);
            }
        
        TInt configType = TPacketDataConfigBase::KConfigGPRS;
        
        switch (gprsProvision->UmtsGprsRelease())
        	{
        	case TPacketDataConfigBase::KConfigGPRS:
        	    configType = TPacketDataConfigBase::KConfigGPRS;
        		break;
        	case TPacketDataConfigBase::KConfigRel99Rel4:
        	    configType = TPacketDataConfigBase::KConfigRel99Rel4;
        		break;
        	case TPacketDataConfigBase::KConfigRel5:
        	    configType = TPacketDataConfigBase::KConfigRel5;
        		break;
        	default:
        	    // we do not have to set provision failure here
        	    // as it was set in the base class constructor
        	    // svg will show the leave error code though
        		User::Leave(KErrNotSupported);
        		break;
        	}
        
        // a provisioning failure would be unrecoverable as this only happens
        // we cannot create memory, access etel or there is a configuration
        // problem - however, leaving with an error here does not stop the
        // sequence of events because the handler for provision config is not
        // expecting a response. if there is a failure then it will be errored in 
        // the next activity in the sequence, i.e. DataClientStart
        TRAP(tNode.iProvisionFailure,tNode.iPdpFsmInterface->NewL(tsyProvision->iTsyName,configType));
        
        tNode.iDefaultSCPR = static_cast<CPDPDefaultSubConnectionProvider*>(&tNode);
        }
    }


//-=========================================================
//Creating context
//-=========================================================
MeshMachine::CNodeActivityBase* CStartActivity::NewL( const MeshMachine::TNodeActivity& aActivitySig, MeshMachine::AMMNodeBase& aNode )
    {
    return new (ELeave) CStartActivity(aActivitySig, aNode);
    };

CStartActivity::~CStartActivity()
    {
    RNodeInterface* startedDataClient = iNode.GetFirstClient<TDefaultClientMatchPolicy>(TClientType(TCFClientType::EData, TCFClientType::EStarted));
    ASSERT(iNode.GetClientIter<TDefaultClientMatchPolicy>(TClientType(TCFClientType::EData))[1] == NULL);
    CPDPSubConnectionProvider& pdpscpr = static_cast<CPDPSubConnectionProvider&>(iNode);
    if (pdpscpr.iPDPFsmContextId == CPDPSubConnectionProvider::EInvalidContextId ||
        startedDataClient == NULL)
        {
        pdpscpr.LinkDown(Error());
        }
    }

CStartActivity::CStartActivity( const MeshMachine::TNodeActivity& aActivitySig, MeshMachine::AMMNodeBase& aNode)
:MeshMachine::CNodeRetryActivity(aActivitySig, aNode)
    {
    }

DEFINE_SMELEMENT(TCreateSecondaryOrMbmsPDPCtx, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TCreateSecondaryOrMbmsPDPCtx::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);

  	RParameterFamily mbmsInfoFamily = iContext.Node().iParameterBundle.FindFamily(KSubConChannelParamsType);
  	STypeId typeId = STypeId::CreateSTypeId(KSubConChannelParamsImplUid, KSubConChannelParamsType);
    CSubConChannelParamSet *mbmsSubConParamSet = mbmsInfoFamily.IsNull() ? NULL :
    		(CSubConChannelParamSet *)(mbmsInfoFamily.FindParameterSet(typeId, RParameterFamily::ERequested));

    if (mbmsSubConParamSet)
		{
		RPacketMbmsContext::TContextConfigMbmsV1 mbmsConfig;
		ConnectionServ::TMBMSChannelInfoV1* channelPtr = reinterpret_cast<ConnectionServ::TMBMSChannelInfoV1*>(mbmsSubConParamSet->GetChannelInfo());
   		mbmsConfig.iTmgi		 		= channelPtr->GetTmgi();
   	 	mbmsConfig.iMbmsServicePriority = channelPtr->GetServicePriority();
   		mbmsConfig.iMbmsAccessBearer    = channelPtr->GetScope();

   		TSessionOperatioInfo sessionInfo;
		CSubConMBMSExtensionParamSet *mbmsSessionSet=static_cast<CSubConMBMSExtensionParamSet *>(mbmsInfoFamily.FindParameterSet(
			STypeId::CreateSTypeId(KSubConMBMSExtParamsImplUid,KSubConMBMSSessionExtParamsType),RParameterFamily::ERequested));
		if (mbmsSessionSet)
			{
			sessionInfo.iSessionIds.Reset();
			for(TInt i=0;i<mbmsSessionSet->GetSessionCount();i++)
				{
				sessionInfo.iSessionIds.Append(mbmsSessionSet->GetSessionId(i));
				}
			sessionInfo.iOperation = mbmsSessionSet->GetOperationType();
			mbmsConfig.iMbmsServiceMode  = mbmsSessionSet->GetServiceMode();
			}
		else
			{
			User::Leave(KErrCorrupt);
			}

		iContext.Node().iPDPFsmContextId = iContext.Node().iPdpFsmInterface->NewFsmContextL(iContext.Node(),SpudMan::EMbms);

		iContext.Node().iContextType=SpudMan::EMbms;

       	iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId,mbmsConfig);

		TTFTInfo tft;
    	User::LeaveIfError(iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, tft));
    	User::LeaveIfError(iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, sessionInfo));
    	User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::ECreateMbmsPDPContext));
    	iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    	sessionInfo.iSessionIds.Close();
    	}
	else
		{
		// Reset the default QoS and TFT here. KContextQoSSet should arrive soon
		// with the proper values.
#ifdef SYMBIAN_NETWORKING_UMTSR5
		RPacketQoS::TQoSR5Requested qos;
#else
		RPacketQoS::TQoSR99_R4Requested qos;
#endif
		// SYMBIAN_NETWORKING_UMTSR5

		iContext.Node().iPDPFsmContextId = iContext.Node().iPdpFsmInterface->NewFsmContextL(iContext.Node(),SpudMan::ESecondary);

		iContext.Node().iContextType = SpudMan::ESecondary;

		User::LeaveIfError(iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId,  qos));

		TTFTInfo tft;
		User::LeaveIfError(iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, tft));
		User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::ECreateSecondaryPDPContext));
		iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
		}
    }

DEFINE_SMELEMENT(TAwaitingPDPCtxCreated, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPDPCtxCreated::Accept()
    {
    if (iContext.Node().iContextType != SpudMan::EMbms)
    	{
    	return TAwaitingPDPFSMMessage::Accept(KSecondaryContextCreated);
    	}
    else
    	{
       	return TAwaitingPDPFSMMessage::Accept(KMbmsContextCreated); //
		}
    }

DEFINE_SMELEMENT(TAwaitingPrimaryPDPCtxCreated, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPrimaryPDPCtxCreated::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KPrimaryContextCreated);
    }

DEFINE_SMELEMENT(TCreatePrimaryPDPCtx, NetStateMachine::MStateTransition, PDPSCprStates::TDefContext)
void TCreatePrimaryPDPCtx::SetupSipServerAddrRetrievalL(RPacketContext::TProtocolConfigOptionV2& aPco)
	{
	TPtr8 pcoPtr(const_cast<TUint8*>(aPco.iMiscBuffer.Ptr()),aPco.iMiscBuffer.Length(),aPco.iMiscBuffer.MaxLength());

	// attach TTlv to the buffer
	TTlvStruct<RPacketContext::TPcoId,RPacketContext::TPcoItemDataLength> tlv(pcoPtr,0);
	tlv.AppendItemL(RPacketContext::TPcoId(RPacketContext::EEtelPcktPCSCFAddressRequest),
		TPtr8(static_cast<TUint8*>(NULL), 0, 0));
	aPco.iMiscBuffer.SetLength(pcoPtr.Length());
	}

void TCreatePrimaryPDPCtx::SetImsSignallingFlagL(RPacketContext::TProtocolConfigOptionV2& aPco, TBool aImcn)
	{
	if (aImcn)
		{
		TPtr8 pcoPtr(const_cast<TUint8*>(aPco.iMiscBuffer.Ptr()),aPco.iMiscBuffer.Length(),aPco.iMiscBuffer.MaxLength());
		TTlvStruct<RPacketContext::TPcoId,RPacketContext::TPcoItemDataLength> tlv(pcoPtr,0);
		tlv.AppendItemL(RPacketContext::TPcoId(RPacketContext::EEtelPcktIMCNMSSubsystemSignallingFlag ),
						TPtr8(static_cast<TUint8*>(NULL), 0, 0));
		aPco.iMiscBuffer.SetLength(pcoPtr.Length());
		}
	}

TBool TCreatePrimaryPDPCtx::IsModeGsmL() const
	{
    const CTSYProvision* tsyProvision = static_cast<const CTSYProvision*>(
    	iContext.Node().AccessPointConfig().FindExtension(CTSYProvision::TypeId()));
	if (tsyProvision == NULL)
		{
		User::Leave(KErrCorrupt);
		}

	RTelServer telServer;
	User::LeaveIfError(telServer.Connect());
	CleanupClosePushL(telServer);

	User::LeaveIfError(telServer.LoadPhoneModule(tsyProvision->iTsyName));

	TInt phones = 0;
	User::LeaveIfError(telServer.EnumeratePhones(phones));

	RTelServer::TPhoneInfo phoneInfo;
	TBuf<KCommsDbSvrMaxFieldLength> currentTsyName;
	TBool found = EFalse;
	_LIT(KTsyNameExtension, ".tsy");
	for (TInt i = 0; i < phones; ++i)
		{
		User::LeaveIfError(telServer.GetTsyName(i, currentTsyName));
		// remove .TSY extension if necessary
		if (currentTsyName.Right(4).CompareF(KTsyNameExtension()) == 0)
			{
			currentTsyName = currentTsyName.Left(currentTsyName.Length() - 4);
			}
		// compare current TSY name with the one we're looking for
		if (currentTsyName.CompareF(tsyProvision->iTsyName) == 0)
			{
			// get phone info from the TSY
			User::LeaveIfError(telServer.GetPhoneInfo(i, phoneInfo));
			found = ETrue;
			break;
			}
		}
	if (!found)
		{
		User::Leave(KErrNotFound);
		}

	RMobilePhone phone;
	User::LeaveIfError(phone.Open(telServer, phoneInfo.iName));
	CleanupClosePushL(phone);

	RMobilePhone::TMobilePhoneNetworkMode networkMode = RMobilePhone::ENetworkModeUnknown;
	User::LeaveIfError(phone.GetCurrentMode(networkMode));
	CleanupStack::PopAndDestroy(2); // phone, telServer
	return (networkMode == RMobilePhone::ENetworkModeGsm);
	}

void TCreatePrimaryPDPCtx::DoL()
    {
    // if the provisionconfig failed, there is no way to inform the CPR of the failure
    // as the framework doesn't expect a response from provisionconfig, so error here
    // if there was a problem so that the appropriate clean up can happen.
    
    CPDPDefaultSubConnectionProvider &tNode = static_cast<CPDPDefaultSubConnectionProvider&>(iContext.Node());
    
    User::LeaveIfError(tNode.iProvisionFailure);
    
    ASSERT(tNode.iPdpFsmInterface);

    iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
            iContext.NodeId(),
            TCFMessage::TStateChange(
                    Elements::TStateChange(KPsdStartingConfiguration, KErrNone)).CRef(),
            TClientType(TCFClientType::ECtrlProvider));

	CGPRSProvision* gprsProvision = const_cast<CGPRSProvision*>(static_cast<const CGPRSProvision*>(
	    iContext.Node().AccessPointConfig().FindExtension(STypeId::CreateSTypeId(CGPRSProvision::EUid,CGPRSProvision::ETypeId))));

	//retrieve QoS (should be provisioned, but can also be overriden with SetParams).
	RPacketQoS::TQoSR5Requested qosOverridenParams;
	const RPacketQoS::TQoSR5Requested* qosParams = NULL;
	if (! iContext.Node().iParameterBundle.IsNull() && ! iContext.Node().iParameterBundle.FindFamily(KSubConQoSFamily).IsNull())
    	{
    	MPDPParamMapper::MapQosParamBundleToEtelL(iContext.Node().iParameterBundle, qosOverridenParams);
    	qosParams = &qosOverridenParams;
    	}
	else
    	{
    	const CDefaultPacketQoSProvision* defaultQoSProvision = static_cast<const CDefaultPacketQoSProvision*>(
    	    iContext.Node().AccessPointConfig().FindExtension(STypeId::CreateSTypeId(CDefaultPacketQoSProvision::EUid,CDefaultPacketQoSProvision::ETypeId)));
    	qosParams = defaultQoSProvision ? &defaultQoSProvision->iParams : NULL;

        //Here we're taking the qos defaults from the provision info, hence skipping the iParameterBundle.
        //The lack of iParameterBundle however and the respective ERequested params is badly tolerated by the rest
        //of the code (e.g.: when subsequently raising granted params it is assumed something has been requested).
        //Let's create a phoney requested params.
        //iContext.Node().CreateParameterBundleL();
		//RParameterFamily family = iContext.Node().iParameterBundle.CreateFamilyL(KSubConQoSFamily); //PJLEFT

		RCFParameterFamilyBundle newBundle;
		newBundle.CreateL();
		iContext.Node().iParameterBundle.Open(newBundle);
		RParameterFamily family = newBundle.CreateFamilyL(KSubConQoSFamily);
		CSubConQosGenericParamSet::NewL(family, RParameterFamily::ERequested);
    	}
	TTFTInfo tft; //We'll use empty/thus default TFT
	if (gprsProvision == NULL || qosParams == NULL)
    	{
    	User::Leave(KErrCorrupt);
    	}

	const CImsExtProvision* imsprov = static_cast<const CImsExtProvision*>(
		iContext.Node().AccessPointConfig().FindExtension(STypeId::CreateSTypeId(CImsExtProvision::EUid, CImsExtProvision::ETypeId)));


	switch (gprsProvision->UmtsGprsRelease())
			{
	    	case TPacketDataConfigBase::KConfigGPRS:
				{
				SetImsSignallingFlagL(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigGPRS>().iProtocolConfigOption, imsprov->iImsSignalIndicator);
				}
				break;
	    	case TPacketDataConfigBase::KConfigRel5:
		    case TPacketDataConfigBase::KConfigRel99Rel4:
				{
				SetImsSignallingFlagL(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigR99_R4>().iProtocolConfigOption, imsprov->iImsSignalIndicator);
				}
				break;
			}

	TRAP_IGNORE(iContext.Node().iIsModeGsm = IsModeGsmL());

	// Only request SIP server address retrieval when network not in GSM/GPRS mode
	// e.g. UMTS/WCDMA
	if (!iContext.Node().iIsModeGsm)
		{
		switch (gprsProvision->UmtsGprsRelease())
			{
	    	case TPacketDataConfigBase::KConfigGPRS:
				{
				SetupSipServerAddrRetrievalL(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigGPRS>().iProtocolConfigOption);
				}
				break;
	    	case TPacketDataConfigBase::KConfigRel5:
		    case TPacketDataConfigBase::KConfigRel99Rel4:
				{
				SetupSipServerAddrRetrievalL(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigR99_R4>().iProtocolConfigOption);
				}
				break;
			}
		}
	
	iContext.Node().iPDPFsmContextId = iContext.Node().iPdpFsmInterface->NewFsmContextL(iContext.Node(),SpudMan::EPrimary);

    iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
            iContext.NodeId(),
            TCFMessage::TStateChange(
                    Elements::TStateChange(KPsdFinishedConfiguration, KErrNone)).CRef(),
            TClientType(TCFClientType::ECtrlProvider));

    iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
            iContext.NodeId(),
            TCFMessage::TStateChange(
                    Elements::TStateChange(KPsdStartingActivation, KErrNone)).CRef(),
            TClientType(TCFClientType::ECtrlProvider));

	ASSERT(iContext.Node().iPDPFsmContextId == KPrimaryContextId);
	iContext.Node().iContextType=SpudMan::EPrimary;
	iContext.Node().iPdpFsmInterface->Set(KPrimaryContextId, gprsProvision->GetScratchContextAs<TPacketDataConfigBase>());
	//Set default QoS
	iContext.Node().iPdpFsmInterface->Set(KPrimaryContextId, *qosParams);
    //Set default TFTs
    iContext.Node().iPdpFsmInterface->Set(KPrimaryContextId,  tft); // ignore any error
    //Start the primary.
    User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(KPrimaryContextId, SpudMan::ECreatePrimaryPDPContext));
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }

DEFINE_SMELEMENT(TOverrideProvision, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TOverrideProvision::DoL()
    {
	const CBCAProvision& bcaExtension = static_cast<const CBCAProvision&>(iContext.Node().AccessPointConfig().FindExtensionL(CBCAProvision::TypeId()));
   		
    // This cannot return NULL - checked in an earlier transition
	const CTSYProvision& tsyProvision = static_cast<const CTSYProvision&>(
        iContext.Node().AccessPointConfig().FindExtensionL(CTSYProvision::TypeId()));
	
	RPacketContext::TDataChannelV2& dataChannel = iContext.Node().iTempDataChannelV2;
	iContext.Node().iPdpFsmInterface->Get(iContext.Node().iPDPFsmContextId, dataChannel);

	// The telephony etel documentation is confusing/misleading.
	// In source it states that both iChannel and iPort have been deprecated, and that iPort is undeprecated
	// and that iPort should be used.
	// Other etel documentation states that iPort is deprecated and that iChannel should be used.
	// Current CBS TA advise suggests iChannelId should be used rather than iPort
	// Legacy SPUDs and this PDP SCpr use iPort and will do this when common TSY is not used maintaining compatibility with existing licensee TSYs
	
	_LIT(KCommonTsy, "phonetsy");
	if (!tsyProvision.iTsyName.Left(KCommonTsy().Length()).CompareF(KCommonTsy))
		{
		// CTSY has been changed to return the context name + channel Id in iChannelId,
		const_cast<CBCAProvision&>(bcaExtension).SetPortName(dataChannel.iChannelId);
		}
	else
		{
		// Co-incidentally SIMTSY will return the same value in both iChannelId and iPort meaning that
		// a change here would not show a regression in our tests - YOU HAVE BEEN CAUTIONED!
		const_cast<CBCAProvision&>(bcaExtension).SetPortName(dataChannel.iPort);
    	}
	
	if (iContext.Node().iContextType != SpudMan::EMbms)
    	{
		CGPRSProvision& gprsProvision = const_cast<CGPRSProvision&>(static_cast<const CGPRSProvision&>(
	    	iContext.Node().AccessPointConfig().FindExtensionL(STypeId::CreateSTypeId(CGPRSProvision::EUid,CGPRSProvision::ETypeId))));
		iContext.Node().iPdpFsmInterface->Get(iContext.Node().iPDPFsmContextId, gprsProvision.GetScratchContextAs<TPacketDataConfigBase>());
    	}
	iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
	        iContext.NodeId(),
	        TCFMessage::TStateChange(
	                Elements::TStateChange(KPsdFinishedActivation, KErrNone)).CRef(),
	                TClientType(TCFClientType::ECtrlProvider));
    }

DEFINE_SMELEMENT(TSendErrorRecoveryRequest, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendErrorRecoveryRequest::DoL()
	{
	TPDPFSMMessages::TPDPFSMMessage* msg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(&iContext.iMessage);

	__ASSERT_DEBUG(msg, User::Panic(KPdpSCprPanic,  CorePanics::KPanicIncorrectMessage));
	__ASSERT_DEBUG(iContext.iNodeActivity, User::Panic(KPdpSCprPanic, CorePanics::KPanicNoActivity));

	const RNodeInterface* controlProvider = iContext.Node().ControlProvider();
	__ASSERT_DEBUG(controlProvider, User::Panic(KPdpSCprPanic, CorePanics::KPanicNoControlProvider));

	
	TErrContext ctx(controlProvider->RecipientId(), msg->MessageId(), iContext.iNodeActivity->ActivitySigId(), Elements::TStateChange(0, msg->iValue2));

	TEErrorRecovery::TErrorRecoveryRequest rawReq(ctx);
	
	
	iContext.iNodeActivity->PostRequestTo(
		*controlProvider,
		TCFSafeMessage::TRequestCarrierEast<TEErrorRecovery::TErrorRecoveryRequest>(rawReq).CRef()
		);
	iContext.iNodeActivity->ClearPostedTo();

	iContext.Node().SetContentionRequested(ETrue);
	}



//-=========================================================
//Activiating context
//-=========================================================

DEFINE_SMELEMENT(TActivatePDPContext, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TActivatePDPContext::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);
    User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EContextActivate));

    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }


DEFINE_SMELEMENT(TAwaitingPDPContextActive, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPDPContextActive::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextActivateEvent);
    }

DEFINE_SMELEMENT(TModifyActivePDPContext, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TModifyActivePDPContext::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);

    TBool QoSChanged=MPDPParamMapper::QoSRequested(iContext.Node().iParameterBundle);

    const RParameterFamily &IPAddressInfoFamily = iContext.Node().iParameterBundle.FindFamily(KSubConIPAddressInfoFamily);
    const CSubConIPAddressInfoParamSet* IPAddressInfoSet = IPAddressInfoFamily.IsNull() ? NULL : static_cast<const CSubConIPAddressInfoParamSet*>(
               IPAddressInfoFamily.FindParameterSet(STypeId::CreateSTypeId(CSubConIPAddressInfoParamSet::EUid,CSubConIPAddressInfoParamSet::ETypeId),RParameterFamily::ERequested));

   	TBool TFTChanged = IPAddressInfoSet?ETrue:EFalse;

   	if(QoSChanged || TFTChanged)
   	    {
        User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EContextModifyActive));
   	    }
   	else
   	    {
        RClientInterface::OpenPostMessageClose(TNodeCtxId(iContext.ActivityId(), iContext.NodeId()), iContext.NodeId(),
            TPDPFSMMessages::TPDPFSMMessage(KContextModifyActiveEvent, KErrNone).CRef());
   	    }

    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }

DEFINE_SMELEMENT(TAwaitingActivePDPContextModified, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingActivePDPContextModified::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextModifyActiveEvent);
    }

DEFINE_SMELEMENT(TAwaitingNegotiatedQoSRetrieved, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingNegotiatedQoSRetrieved::Accept()
    {

    return TAwaitingPDPFSMMessage::Accept(KGetNegQoSEvent);
    }

DEFINE_SMELEMENT(TGetNegotiatedQoS, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TGetNegotiatedQoS::DoL()
    {
    if (iContext.Node().iContextType == SpudMan::EMbms)
    	{
    	//just move to next step
    	RClientInterface::OpenPostMessageClose(
    			TNodeCtxId(iContext.ActivityId(), iContext.NodeId()),
    			iContext.NodeId(),
    			TPDPFSMMessages::TPDPFSMMessage(KGetNegQoSEvent, KErrNone).CRef()
    			);
    	}
    else
    	{
    	ASSERT(iContext.Node().iPdpFsmInterface);
    	User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EGetNegQoS));
    	}
    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }


DEFINE_SMELEMENT(TSendDataClientStarted, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendDataClientStarted::DoL()
    {
    PRStates::TSendDataClientStarted coreDCStarted(iContext);
    coreDCStarted.DoL();
    iContext.Node().LinkUp();
    }

DEFINE_SMELEMENT(TSendDataClientStopped, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendDataClientStopped::DoL()
    {
    PRStates::TSendDataClientStopped coreDCStopped(iContext);
    coreDCStopped.DoL();
    iContext.Node().LinkDown(coreDCStopped.iStopCode);
    }



//-=========================================================
//Setting Params
//-=========================================================
DEFINE_SMELEMENT(TSetQoS, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSetQoS::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);
    ASSERT(!iContext.Node().GetParameterBundle().IsNull());

    if (MPDPParamMapper::QoSRequested(iContext.Node().GetParameterBundle()))
        {
        RPacketQoS::TQoSR5Requested requestedParams;
        iContext.Node().iParamsRelease = MPDPParamMapper::MapQosParamBundleToEtelL(iContext.Node().GetParameterBundle(), requestedParams);
        iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, requestedParams);
        User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EContextQoSSet));
        }
    else
        {
        //No QoS Requested. Complete this message locally to push yourself forward
        RClientInterface::OpenPostMessageClose(TNodeCtxId(iContext.ActivityId(), iContext.NodeId()), iContext.NodeId(),
        	TPDPFSMMessages::TPDPFSMMessage(KContextQoSSetEvent, KErrNone).CRef());
        }

    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }

DEFINE_SMELEMENT(TAwaitingQoSSet, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingQoSSet::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextQoSSetEvent);
    }

DEFINE_SMELEMENT(TSetTFT, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSetTFT::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);
    ASSERT(!iContext.Node().GetParameterBundle().IsNull());

	RParameterFamily ipAddressInfoFamily =
		iContext.Node().GetParameterBundle().FindFamily(KSubConIPAddressInfoFamily);
	CSubConIPAddressInfoParamSet* ipAddressInfoSet = NULL;
	if( ! ipAddressInfoFamily.IsNull())
		{
		ipAddressInfoSet = static_cast<CSubConIPAddressInfoParamSet*>(
			ipAddressInfoFamily.FindParameterSet(
				STypeId::CreateSTypeId(CSubConIPAddressInfoParamSet::EUid,CSubConIPAddressInfoParamSet::ETypeId),
					RParameterFamily::ERequested));
		}

    if (ipAddressInfoSet)
        {
        iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, iContext.Node().GetTftInfoL(ipAddressInfoSet));
        iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, iContext.Node().GetOperationCodeL(ipAddressInfoSet));
        User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EContextTFTModify));
        }
    else
        {
        //No QoS Requested. Complete this message locally to push yourself forward
        RClientInterface::OpenPostMessageClose(TNodeCtxId(iContext.ActivityId(), iContext.NodeId()), iContext.NodeId(),
        	TPDPFSMMessages::TPDPFSMMessage(KContextTFTModifiedEvent, KErrNone).CRef());
        }

    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }

DEFINE_SMELEMENT(TAwaitingTFTSet, NetStateMachine::MState, PDPSCprStates::TDefContext)
TBool TAwaitingTFTSet::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextTFTModifiedEvent);
    }



//-=========================================================
//Setting MBMS Params (session ids)
//-=========================================================
DEFINE_SMELEMENT(TSetMbmsParameters, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSetMbmsParameters::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);
    if (iContext.Node().iContextType == SpudMan::EMbms)
		{
		/* extract session ids from MBMS Extension Set*/
		RParameterFamily mbmsInfoFamily = iContext.Node().iParameterBundle.FindFamily(KSubConChannelParamsType);
   		CSubConMBMSExtensionParamSet *mbmsExtensionSet = mbmsInfoFamily.IsNull() ? 	NULL :
   		   static_cast<CSubConMBMSExtensionParamSet *>(mbmsInfoFamily.FindParameterSet(
   		   		STypeId::CreateSTypeId(KSubConMBMSExtParamsImplUid,KSubConMBMSSessionExtParamsType), RParameterFamily::ERequested));
		if (mbmsExtensionSet)
			{
			TSessionOperatioInfo sessionInfo;
			sessionInfo.iSessionIds.Reset();
			for(int i=0 ;i < mbmsExtensionSet->GetSessionCount(); i++)
				{
				sessionInfo.iSessionIds.Append(mbmsExtensionSet->GetSessionId(i));
				}
			sessionInfo.iOperation=mbmsExtensionSet->GetOperationType();
			User::LeaveIfError(iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, sessionInfo));
			User::LeaveIfError(iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EMbmsSessionUpdate));
			sessionInfo.iSessionIds.Close();
			}
		}
	}

//-=========================================================
//Events
//-=========================================================
DEFINE_SMELEMENT(TBlockedOrUnblocked, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TBlockedOrUnblocked::TransitionTag()
	{
	TPDPFSMMessages::TPDPFSMMessage& pdpmsg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);
	if (pdpmsg.iValue1 == KContextBlockedEvent)
		return KBlocked;
	else
		return KUnblocked;
	}



DEFINE_SMELEMENT(TAwaitingParamsChanged, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingParamsChanged::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextParametersChangeEvent);
    }

DEFINE_SMELEMENT(TAwaitingContextBlockedOrUnblocked, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingContextBlockedOrUnblocked::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KContextBlockedEvent) || TAwaitingPDPFSMMessage::Accept(KContextUnblockedEvent);
    }

DEFINE_SMELEMENT(TForwardContextBlockedOrUnblockedToDC, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TForwardContextBlockedOrUnblockedToDC::DoL()
    {
    TPDPFSMMessages::TPDPFSMMessage& pdpmsg = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);

	RNodeInterface* theOnlyDataClient = iContext.iNode.GetFirstClient<TDefaultClientMatchPolicy>(TCFClientType::EData);
	ASSERT(iContext.iNode.GetClientIter<TDefaultClientMatchPolicy>(TCFClientType::EData)[1] == NULL);
    ASSERT(theOnlyDataClient);

    switch (pdpmsg.iValue1)
        {
        case KContextBlockedEvent:
        theOnlyDataClient->PostMessage(iContext.NodeId(), ESock::TCFFlow::TBlock().CRef());
        break;

        case KContextUnblockedEvent:
        theOnlyDataClient->PostMessage(iContext.NodeId(), ESock::TCFFlow::TUnBlock().CRef());
        break;

        default:
        ASSERT(EFalse);
        }
    }

DEFINE_SMELEMENT(TSendDataTransferTemporarilyBlocked, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendDataTransferTemporarilyBlocked::DoL()
    {
    if (iContext.Node().iPDPFsmContextId == KPrimaryContextId)
        {
        iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
                iContext.NodeId(),
                TCFMessage::TStateChange(
                    Elements::TStateChange(KDataTransferTemporarilyBlocked, KErrNone)).CRef(),
                    TClientType(TCFClientType::ECtrlProvider));
        }
	}

DEFINE_SMELEMENT(TSendDataTransferUnblocked, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendDataTransferUnblocked::DoL()
    {
    if (iContext.Node().iPDPFsmContextId == KPrimaryContextId)
        {
        iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
                iContext.NodeId(),
                TCFMessage::TStateChange(
                    Elements::TStateChange(KDataTransferUnblocked, KErrNone)).CRef(),
                    TClientType(TCFClientType::ECtrlProvider));
        }
    }
	
DEFINE_SMELEMENT(TFillInGrantedParams, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TFillInGrantedParams::DoL()
    {
    ASSERT(!iContext.Node().GetParameterBundle().IsNull());
    RCFParameterFamilyBundleC& bundle = iContext.Node().GetParameterBundle();
    TBool notifySetFlag=EFalse;

	CSubConGenEventParamsGranted* event;
	RParameterFamily parFamily;

    //-=============================================
    //Qos Params
    //-=============================================
    //Fish out the qos family
    if (MPDPParamMapper::QoSRequested(bundle))
        {
        //Quite awkward CSubConnectionParameterBundle API doesn't allow browsing
        //the extensions. CSubConGenEventParamsGranted does. Hence the
        //MPDPParamMapper fills in events (and not bundles), as bundles
        //can be populated from the events and not vice versa.
        //It's unfortunate, as we're not even raising an event here.
        //CSubConGenEventParamsGranted & CSubConnectionParameterBundle
        //would benefit greatly from having a common ancestor.
		parFamily = bundle.FindFamily(KSubConQoSFamily);
		if( parFamily.IsNull() )
			{
			// really should have been found!
			User::Leave(KErrUnknown);
			}
		event = CSubConGenEventParamsGranted::NewL();
        CleanupStack::PushL(event);
        event->SetFamily (KSubConQoSFamily);
        FillInEventL(*event); //fill in the params.
        notifySetFlag = ETrue;
        }
	else if (!(parFamily = iContext.Node().iParameterBundle.FindFamily(KSubConAuthorisationFamily)).IsNull()) // check out sblp session ids granted
        {  // check out sblp familytype (KSubConAuthorisationFamily==5)         	    	   			
    			event = CSubConGenEventParamsGranted::NewL();
    			CleanupStack::PushL(event);
    			event->SetFamily (KSubConAuthorisationFamily);
    			FillInEventL(*event); //fill in the params.
    			notifySetFlag = ETrue;
        }
	else //check out mbms session ids granted
        {
        parFamily = iContext.Node().iParameterBundle.FindFamily(KSubConChannelParamsType);
        STypeId typeId = STypeId::CreateSTypeId(KSubConChannelParamsImplUid, KSubConChannelParamsType);
		if (!parFamily.IsNull() && parFamily.FindParameterSet(typeId, RParameterFamily::ERequested))
			{
			event = CSubConGenEventParamsGranted::NewL();
	        CleanupStack::PushL(event);
			event->SetFamily (KSubConChannelParamsType);
			FillInMbmsSessionIdsL(*event); //Fill Session Ids
			notifySetFlag = ETrue;
			}
		}

	if (notifySetFlag)
		{
        parFamily.ClearAllParameters(RParameterFamily::ERequested);
        parFamily.ClearAllParameters(RParameterFamily::EAcceptable);
        parFamily.ClearAllParameters(RParameterFamily::EGranted);

   		//Populate EGranted params from the event.
   		FillInGrantedL(parFamily, *event);

   		iContext.Node().NotifyClientsL(*event);
   		CleanupStack::Pop(event);
		}


    //-=============================================
    //TFT Params
    //-=============================================
	// Should use KSubConIPAddressInfoFamily in FindFamily as argument instead of EUid
	// DEF123513 FindFamily uses EUid as argument raised to fix regressions.
	RParameterFamily ipAddressInfoFamily=bundle.FindFamily(CSubConIPAddressInfoParamSet::EUid);
    if ( ! ipAddressInfoFamily.IsNull())
        {
        CSubConIPAddressInfoParamSet* requestedIPAddressInfo = CSubConIPAddressInfoParamSet::NewL(ipAddressInfoFamily, RParameterFamily::ERequested);
		TInt count = requestedIPAddressInfo->GetParamNum();
		if (count > 0)
			{
		CSubConIPAddressInfoParamSet::TSubConIPAddressInfo paramInfo(requestedIPAddressInfo->GetParamInfoL(0));
		TUint id = iContext.Node().FindPacketFilterIdL(paramInfo);

		switch(paramInfo.iState)
			{
			case CSubConIPAddressInfoParamSet::TSubConIPAddressInfo::EAdd :
					iContext.Node().NewPacketFilterAddedL(paramInfo, id);
					break;
			case CSubConIPAddressInfoParamSet::TSubConIPAddressInfo::ERemove :
					iContext.Node().PacketFilterRemovedL(id);
					break;
			case CSubConIPAddressInfoParamSet::TSubConIPAddressInfo::ENone :
					break;
			default: ;
				/** TODO: What to do with an error */
				}
			}

		ipAddressInfoFamily.ClearAllParameters(RParameterFamily::ERequested);

        }
    //something must have had been granted.
    /* [401TODO]: We have a case where applyrequest after rejoin on transfloshim will cause this bit to execute
    Since we don't have neither qos nor addrInfo parameters set it will kick this assert*/
    }

void TParamsEvent::FillInEventL(CSubConGenEventParamsGranted& aEvent)
    {
    RPacketQoS::TQoSR5Negotiated negotiatedQoS;
    iContext.Node().iPdpFsmInterface->Get(iContext.Node().iPDPFsmContextId,  negotiatedQoS);
    MPDPParamMapper::MapQosEtelToGrantedParamsL(&negotiatedQoS, aEvent, iContext.Node().iParamsRelease);
    }


void TParamsEvent::FillInMbmsSessionIdsL(CSubConGenEventParamsGranted& aEvent)
	{

	CSubConChannelParamSet* genericMbms = CSubConChannelParamSet::NewL ();
	aEvent.SetGenericSet(genericMbms);

	//Fill  List of Session Ids received in Extension Set
	TSessionOperatioInfo sessionInfo;
	User::LeaveIfError(iContext.Node().iPdpFsmInterface->Get(iContext.Node().iPDPFsmContextId, sessionInfo));
	CSubConMBMSExtensionParamSet *mbmsSessionExtn= CSubConMBMSExtensionParamSet::NewL();
	CleanupStack::PushL(mbmsSessionExtn);
	for (TInt i=0;i<sessionInfo.iSessionIds.Count();i++)
		{
		mbmsSessionExtn->SetSessionId(sessionInfo.iSessionIds[i]);
		}
	aEvent.AddExtensionSetL(mbmsSessionExtn);
   	sessionInfo.iSessionIds.Close();
	CleanupStack::Pop(mbmsSessionExtn);
	}

void TParamsEvent::FillInGrantedL(RParameterFamily& aFamily, const CSubConGenEventParamsGranted& aEvent)
    {
    //Unfortunatelly no easy way to copy a content of an event into
    //a bundle family. Using ECOM Load/Store to do that.
    //Unfortunatelly CSubConGenEventParamsGranted::GetNumExtensionSets is a non-const
    //method.
    CSubConGenEventParamsGranted& noncEvent = const_cast<CSubConGenEventParamsGranted&>(aEvent);

    //clear granted params in the bundle
    aFamily.ClearAllParameters(RParameterFamily::EGranted);
    ASSERT(aEvent.GetGenericSet());
    const CSubConParameterSet* genSetSource = aEvent.GetGenericSet();

    TInt i = 0;

    //dry run - find out the max length for the buffer.
    TInt bufLen = genSetSource->Length();
    for (i = 0; i < noncEvent.GetNumExtensionSets(); i++)
        {
        TInt extLen = aEvent.GetExtensionSet(i)->Length();
        bufLen = extLen > bufLen ? extLen : bufLen;
        }


	RBuf8 buffer;
	buffer.CreateL(bufLen);
	buffer.CleanupClosePushL();

	genSetSource->Store(buffer);
    TPtrC8 whyDoWeNeedPointerToRbufHere(buffer);
	CSubConGenericParameterSet* genSetTarget = static_cast<CSubConGenericParameterSet*>(
	                    CSubConGenericParameterSet::LoadL(whyDoWeNeedPointerToRbufHere));
	CleanupStack::PushL(genSetTarget);
    aFamily.AddParameterSetL(genSetTarget, RParameterFamily::EGranted);
    CleanupStack::Pop(genSetTarget);

    //loop thru the extensions
    for (i = 0; i < noncEvent.GetNumExtensionSets(); i++)
        {
        const CSubConExtensionParameterSet* extSetSource = aEvent.GetExtensionSet(i);
        buffer.Zero();
        extSetSource->Store(buffer);
        whyDoWeNeedPointerToRbufHere.Set(buffer);
        CSubConExtensionParameterSet* extSetTarget = static_cast<CSubConExtensionParameterSet*>(CSubConParameterSet::LoadL(whyDoWeNeedPointerToRbufHere));
        CleanupStack::PushL(extSetTarget);
        aFamily.AddParameterSetL(extSetTarget, RParameterFamily::EGranted);
        CleanupStack::Pop(extSetTarget);
        }
    CleanupStack::PopAndDestroy(&buffer);
    }


DEFINE_SMELEMENT(TRaiseParamsRejectedL, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TRaiseParamsRejectedL::DoL()
    {
    ASSERT(iContext.iNodeActivity && iContext.iNodeActivity->Error() != KErrNone);
    CSubConGenEventParamsRejected* event= CSubConGenEventParamsRejected::NewL();
    CleanupStack::PushL(event);

	if (iContext.Node().iContextType == SpudMan::EMbms)
		{
		TPDPFSMMessages::TPDPFSMMessage& message = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);
		TInt prevOperationValue = message.iValue2;
		if ((prevOperationValue == KErrNotFound) || (prevOperationValue == KErrNotSupported ) ||
			 (prevOperationValue == KErrMbmsImpreciseServiceEntries ) ||(prevOperationValue == KErrMbmsServicePreempted) )
			{
    		event->SetFamilyId (KSubConChannelParamsType);
			}
		}
	else
		{
		event->SetFamilyId (KSubConQoSFamily);
		}
    event->SetError(iContext.iNodeActivity->Error());
    iContext.Node().NotifyClientsL(*event);
    CleanupStack::Pop(event);

	TInt err = iContext.iNodeActivity->Error();
	iContext.iNodeActivity->SetError(KErrNone);
	User::Leave(err);
	}

DEFINE_SMELEMENT(TRaiseParamsChanged, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TRaiseParamsChanged::DoL()
    {
    TPDPFSMMessages::TPDPFSMMessage& message = message_cast<TPDPFSMMessages::TPDPFSMMessage>(iContext.iMessage);
	TInt err=KErrNone;
    ASSERT(message.iValue2 == KErrNone);
    CSubConGenEventParamsChanged* event = NULL;
    TRAP_IGNORE(event = CSubConGenEventParamsChanged::NewL());
    if (event)
       	{
       	RParameterFamily parFamily = iContext.Node().iParameterBundle.FindFamily(KSubConChannelParamsType);
    	if (!parFamily.IsNull()) //check the changed mbms session ids
			{
			STypeId typeId = STypeId::CreateSTypeId(KSubConChannelParamsImplUid, KSubConChannelParamsType);
			CSubConChannelParamSet* mbmsSubConParamSet = parFamily.IsNull() ? NULL :
    			static_cast<CSubConChannelParamSet *>(parFamily.FindParameterSet(typeId, RParameterFamily::ERequested));
			if (mbmsSubConParamSet)
				{
				event->SetFamily (KSubConChannelParamsType);
        		event->SetError(message.iValue2);
				TRAP(err,FillInMbmsSessionIdsL(*event)); //Fill Session Ids
				}
			}
		else
			{
			parFamily = iContext.Node().iParameterBundle.FindFamily(KSubConQoSFamily);
			event->SetFamily (KSubConQoSFamily);
        	event->SetError(message.iValue2);
        	TRAP(err, FillInEventL(*event));
			}
		if (err == KErrNone)
          	{
			FillInGrantedL(parFamily, *event);
            iContext.Node().NotifyClientsL(*event);
			}
       	}
   	else
        {
        delete event;
        }
    }


DEFINE_SMELEMENT(TAwaitingNetworkStatusEvent, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingNetworkStatusEvent::Accept()
    {
    return TAwaitingPDPFSMMessage::Accept(KNetworkStatusEvent);
    }

DEFINE_SMELEMENT(TNetworkStatusEventTypeTag, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNetworkStatusEventTypeTag::TransitionTag()
    {
	RPacketService::TStatus status;
	iContext.Node().iPdpFsmInterface->Get(status);
    switch (status)
        {
        case RPacketService::EStatusUnattached:
            // If status change is to unattached, but stop has been requested,
            // or we haven't been started, do nothing
            if (iContext.Node().CountActivities(ECFActivityStopDataClient) > 0
               || iContext.Node().CountClients<TDefaultClientMatchPolicy>(TClientType(TCFClientType::EData, TCFClientType::EStarted)) == 0)
                {
                return KNoTag;
                }
			return status;	
        case RPacketService::EStatusAttached:
        case RPacketService::EStatusActive:
        case RPacketService::EStatusSuspended:
        	return status;
        }
    return KNoTag;
    }

DEFINE_SMELEMENT(TSendGoneDown, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TSendGoneDown::DoL()
    {
    ASSERT(iContext.iNodeActivity);

    const TProviderInfo& providerInfo = static_cast<const TProviderInfoExt&>(iContext.Node().AccessPointConfig().FindExtensionL(
            STypeId::CreateSTypeId(TProviderInfoExt::EUid, TProviderInfoExt::ETypeId))).iProviderInfo; 
	iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
	iContext.NodeId(),
	TCFControlClient::TGoneDown(KErrDisconnected,providerInfo.APId()).CRef(),
		TClientType(TCFClientType::ECtrl));

	iContext.Node().ControlProvider()->PostMessage(
 	        iContext.NodeId(),
 	        TCFControlProvider::TDataClientGoneDown(KErrDisconnected,providerInfo.APId()).CRef());

    }


//-=========================================================
//Destroying context
//-=========================================================
DEFINE_SMELEMENT(TDestroyPDPContext, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TDestroyPDPContext::DoL()
    {
    ASSERT(iContext.Node().iPdpFsmInterface);
    if (iContext.Node().iPDPFsmContextId == KPrimaryContextId)
        {
        iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
                iContext.NodeId(),
                TCFMessage::TStateChange(
                        Elements::TStateChange(KPsdStartingDeactivation, KErrNone)).CRef(),
                        TClientType(TCFClientType::ECtrlProvider));
        }

    iContext.Node().iPdpFsmInterface->Input(iContext.Node().iPDPFsmContextId, SpudMan::EContextDelete);

    //Expect Response
    iContext.iNodeActivity->ClearPostedTo();
    iContext.Node().iActivityAwaitingResponse = iContext.iNodeActivity->ActivityId();
    }

DEFINE_SMELEMENT(TAwaitingPDPContextDestroyed, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPDPContextDestroyed::Accept()
    {
    if (TAwaitingPDPFSMMessage::Accept(KContextDeleteEvent))
        {
        if (iContext.Node().iPDPFsmContextId != CPDPSubConnectionProvider::EInvalidContextId)
            {
			if (!iContext.Node().ContentionRequested())
				{
				CSubConGenEventSubConDown* event = CSubConGenEventSubConDown::NewL();
				CleanupStack::PushL(event);
				iContext.Node().NotifyClientsL(*event);
				CleanupStack::Pop(event);
				}

			if (iContext.Node().iPDPFsmContextId == KPrimaryContextId)
                {
                iContext.Node().PostToClients<TDefaultClientMatchPolicy>(
                        iContext.NodeId(),
                        TCFMessage::TStateChange(
                                Elements::TStateChange(KPsdFinishedDeactivation, KErrNone)).CRef(),
                                TClientType(TCFClientType::ECtrlProvider));
                }

            iContext.Node().iPdpFsmInterface->DeleteFsmContext(iContext.Node().iPDPFsmContextId);
            iContext.Node().iPDPFsmContextId = CPDPSubConnectionProvider::EInvalidContextId;            
            }
        return ETrue;
        }
    return EFalse;
    }

DEFINE_SMELEMENT(TAwaitingPDPContextGoneDown, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingPDPContextGoneDown::Accept()
    {
    if (TAwaitingPDPContextDestroyed::Accept())
        {
		if (!iContext.Node().ContentionRequested())
			{
			//Following SPUD, we're ignoring the error raised by the FSM and override it with KErrDisconnected
			iContext.Node().LinkDown(KErrDisconnected);
			}
        return ETrue;
        }
    return EFalse;
    }


DEFINE_SMELEMENT(TNoTagOrProviderStopped, NetStateMachine::MStateFork, PDPSCprStates::TContext)
TInt TNoTagOrProviderStopped::TransitionTag()
    {
    if (iContext.Node().iPDPFsmContextId != CPDPSubConnectionProvider::EInvalidContextId &&
        iContext.Node().iPdpFsmInterface->FsmContextExists(iContext.Node().iPDPFsmContextId))
        {
        //context is alive
        return KNoTag;
        }
    return CoreNetStates::KProviderStopped;
    }



//===========================================================
//   Sip Address retrieval
//===========================================================
DEFINE_SMELEMENT(TAwaitingIoctlMessage, NetStateMachine::MState, PDPSCprStates::TContext)
TBool TAwaitingIoctlMessage::Accept()
	{
	if (iContext.iMessage.IsTypeOf(Meta::STypeId::CreateSTypeId(TCFSigLegacyRMessage2Ext::EUid, TCFSigLegacyRMessage2Ext::ETypeId)))
		{
		TCFSigLegacyRMessage2Ext& msg = static_cast<TCFSigLegacyRMessage2Ext&>(iContext.iMessage);
		if (msg.iMessage.Function() == ECNIoctl)
			{
			return ETrue;
			}
		}
	return EFalse;
	}

DEFINE_SMELEMENT(TRetrieveSipAddr, NetStateMachine::MStateTransition, PDPSCprStates::TDefContext)
void TRetrieveSipAddr::DoL()
	{
	MeshMachine::CNodeParallelMessageStoreActivityBase* act = static_cast<MeshMachine::CNodeParallelMessageStoreActivityBase*>(iContext.Activity());

	ASSERT(act->Message().IsTypeOf(Meta::STypeId::CreateSTypeId(TCFSigLegacyRMessage2Ext::EUid, TCFSigLegacyRMessage2Ext::ETypeId)));
	RLegacyResponseMsg ioctl(iContext, static_cast<TCFSigLegacyRMessage2Ext&>(act->Message()).iMessage, static_cast<TCFSigLegacyRMessage2Ext&>(act->Message()).iMessage.Int0());
	
	TInt err = KErrNotFound;

	if (iContext.Node().iIsModeGsm)
		{
		err = KErrNotSupported;
		}
	else
		{
		CGPRSProvision* gprsProvision = const_cast<CGPRSProvision*>(
			static_cast<const CGPRSProvision*>(iContext.Node().AccessPointConfig().FindExtension(
												   STypeId::CreateSTypeId(CGPRSProvision::EUid,CGPRSProvision::ETypeId))));

		RPacketContext::TProtocolConfigOptionV2* protConfigOpt = NULL;
		
		if(gprsProvision != NULL)
		   {
		   switch (gprsProvision->UmtsGprsRelease())
		   	  {
	    	  case TPacketDataConfigBase::KConfigGPRS:
			     {
				 protConfigOpt = &(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigGPRS>().iProtocolConfigOption);
				 }
			  break;
			  
	    	  case TPacketDataConfigBase::KConfigRel5:
			  case TPacketDataConfigBase::KConfigRel99Rel4:
			     {
				 protConfigOpt = &(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigR99_R4>().iProtocolConfigOption);
				 }
			  break;
			  }
		   }
		
		if(protConfigOpt != NULL)
			{
			if (protConfigOpt->iMiscBuffer.Length() > 0)
				{
				TSipServerAddrBuf sipbuf;
				ioctl.ReadL(2, sipbuf);

				TPtr8 pcoPtr(const_cast<TUint8*>(protConfigOpt->iMiscBuffer.Ptr()),
						protConfigOpt->iMiscBuffer.Length(),
						protConfigOpt->iMiscBuffer.MaxLength());
				TTlvStruct<RPacketContext::TPcoId,RPacketContext::TPcoItemDataLength> tlv(pcoPtr,0);
				tlv.ResetCursorPos();

				TIp6Addr addr;
				TPtr8 addrPtr(NULL, 0);
				TPckg<TIp6Addr> addrPckg(addr);

				for (TInt i = 0; tlv.NextItemL(RPacketContext::EEtelPcktPCSCFAddress,addrPtr) == KErrNone; i++)
					{
					if (i == sipbuf().index)
						{
						addrPckg.Copy(addrPtr);
						sipbuf().address.SetAddress(addr);
						if (!sipbuf().address.IsUnspecified())
							err = KErrNone;
						break;
						}
					}

				ioctl.WriteL(2, sipbuf);
				}
			else
				{
				err = KErrNotFound;
				}
			}
		}

	ioctl.Complete(err);
	}


//===========================================================
// Rejoin
//===========================================================

MeshMachine::CNodeActivityBase* CPrimaryPDPGoneDownActivity::NewL(const MeshMachine::TNodeActivity& aActivitySig, MeshMachine::AMMNodeBase& aNode)
    {
    return new (ELeave) CPrimaryPDPGoneDownActivity(aActivitySig, aNode);
    }

CPrimaryPDPGoneDownActivity::~CPrimaryPDPGoneDownActivity()
    {
    }

CPDPSubConnectionProvider* CPrimaryPDPGoneDownActivity::NewDefault()
    {
    if (iNewDefault == NULL)
        {
    	THighestQoSQuery query(static_cast<CPDPSubConnectionProvider&>(iNode).ControlProvider()->RecipientId());
    	static_cast<CSubConnectionProviderFactoryBase&>(static_cast<CPDPSubConnectionProvider&>(iNode).Factory()).Find(query);
    	iNewDefault = query.HighestQoSProvider();
        }
    return iNewDefault;
    }

CPrimaryPDPGoneDownActivity::CPrimaryPDPGoneDownActivity(const MeshMachine::TNodeActivity& aActivitySig, MeshMachine::AMMNodeBase& aNode )
:MeshMachine::CNodeActivityBase(aActivitySig, aNode)
    {
	iOriginalDataClient.SetNull();
    }


DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TApplyNewDefault, NetStateMachine::MStateTransition, CPrimaryPDPGoneDownActivity::TContext)
void CPrimaryPDPGoneDownActivity::TApplyNewDefault::DoL()
	{
    ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* primaryGoneDownActivity = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);

    RClientInterface::OpenPostMessageClose(TNodeCtxId(iContext.ActivityId(), iContext.NodeId()), primaryGoneDownActivity->NewDefault()->Id(),
    	TCFScpr::TApplyRequest().CRef());
	}

DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TStoreOriginalDataClient, NetStateMachine::MStateTransition, CPrimaryPDPGoneDownActivity::TContext)
void CPrimaryPDPGoneDownActivity::TStoreOriginalDataClient::DoL()
	{
	ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* act = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);
	act->iOriginalDataClient
		= iContext.Node().GetFirstClient<TDefaultClientMatchPolicy>(TClientType(TCFClientType::EData))->RecipientId();
	}

DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TStopOriginalDataClient, NetStateMachine::MStateTransition, CPrimaryPDPGoneDownActivity::TContext)
void CPrimaryPDPGoneDownActivity::TStopOriginalDataClient::DoL()
	{
	ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* act = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);
	ASSERT(!act->iOriginalDataClient.IsNull());

	iContext.iNodeActivity->PostRequestTo(act->iOriginalDataClient,
		TCFDataClient::TStop(KErrCancel).CRef());
	}


DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TSwitchToNewDefault, NetStateMachine::MStateTransition, CPrimaryPDPGoneDownActivity::TContext)
void CPrimaryPDPGoneDownActivity::TSwitchToNewDefault::DoL()
	{
  	ASSERT(&iContext.Node() == iContext.Node().iDefaultSCPR); //only to be excecuted by the current default
	ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* primaryGoneDownActivity = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);
	CPDPSubConnectionProvider* newDefault = primaryGoneDownActivity->NewDefault();
	ASSERT( newDefault && newDefault != &iContext.Node());
    newDefault->RemoveClient(iContext.NodeId());

	//switch the context owner - the current owner will go down.
	iContext.Node().iPDPFsmContextId = newDefault->iPDPFsmContextId;
	newDefault->iPDPFsmContextId = CPDPSubConnectionProvider::EInvalidContextId;
	iContext.Node().iPdpFsmInterface->Set(iContext.Node().iPDPFsmContextId, iContext.Node());
	}

DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TRejoinDataClient, NetStateMachine::MStateTransition, CPrimaryPDPGoneDownActivity::TContext)
void CPrimaryPDPGoneDownActivity::TRejoinDataClient::DoL()
	{
  	ASSERT(&iContext.Node() == iContext.Node().iDefaultSCPR); //only to be excecuted by the current default
	ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* primaryGoneDownActivity = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);
	CPDPSubConnectionProvider* newDefault = primaryGoneDownActivity->NewDefault();
	ASSERT( newDefault && newDefault != &iContext.Node());
	if(!newDefault)
	{
		User::Leave(KErrNotFound);
	}
	newDefault->AddClientL(iContext.NodeId(), TClientType(TCFClientType::ECtrl));

	//migrate the flow - will need TApply
    TCFRejoiningProvider::TRejoinDataClientRequest msg(newDefault->GetFirstClient<TDefaultClientMatchPolicy>(TClientType(TCFClientType::EData))->RecipientId(), iContext.NodeId());
    RClientInterface::OpenPostMessageClose(TNodeCtxId(iContext.ActivityId(), iContext.NodeId()), newDefault->Id(), msg);
	}


DEFINE_SMELEMENT( CPrimaryPDPGoneDownActivity::TNoTagOrProviderStopped, NetStateMachine::MStateFork, CPrimaryPDPGoneDownActivity::TContext)
TInt CPrimaryPDPGoneDownActivity::TNoTagOrProviderStopped::TransitionTag()
	{
	ASSERT(iContext.iNodeActivity);
	CPrimaryPDPGoneDownActivity* primaryGoneDownActivity = static_cast<CPrimaryPDPGoneDownActivity*>(iContext.iNodeActivity);
	if (primaryGoneDownActivity->NewDefault())
    	{
    	return KNoTag;
    	}
    return CoreNetStates::KProviderStopped;
	}

DEFINE_SMELEMENT(TFillInImsExtParams, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TFillInImsExtParams::DoL()
	{
	CGPRSProvision* gprsProvision = const_cast<CGPRSProvision*>(
			static_cast<const CGPRSProvision*>(iContext.Node().AccessPointConfig().FindExtension(
					STypeId::CreateSTypeId(CGPRSProvision::EUid,CGPRSProvision::ETypeId))));
	ASSERT(gprsProvision);

	iContext.Node().iPdpFsmInterface->Get(KPrimaryContextId, gprsProvision->GetScratchContextAs<TPacketDataConfigBase>());
	// the bundle is assumed to be valid, since its created in createPdpContext
	//RParameterFamily imcnFamily = iContext.Node().GetOrCreateParameterBundleL().CreateFamilyL(KSubConnContextDescrParamsFamily); //PJLEFT

	RCFParameterFamilyBundle newBundle;
	newBundle.CreateL();
	newBundle.Open(iContext.Node().iParameterBundle);
	RParameterFamily imcnFamily = newBundle.CreateFamilyL(KSubConnContextDescrParamsFamily);

	CSubConImsExtParamSet *imcnFlag = CSubConImsExtParamSet::NewL(imcnFamily,RParameterFamily::EGranted);
	newBundle.Close();

	RPacketContext::TProtocolConfigOptionV2* pco = NULL;
	switch (gprsProvision->UmtsGprsRelease())
		{
    		case TPacketDataConfigBase::KConfigGPRS:
			{
			pco = &(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigGPRS>().iProtocolConfigOption);
			}
			break;
	    	case TPacketDataConfigBase::KConfigRel5:
	    	case TPacketDataConfigBase::KConfigRel99Rel4:
			{
			pco = &(gprsProvision->GetScratchContextAs<RPacketContext::TContextConfigR99_R4>().iProtocolConfigOption);
			}
			break;
		}

	if(pco)
		{
		TPtr8 pcoPtr(const_cast<TUint8*>(pco->iMiscBuffer.Ptr()),pco->iMiscBuffer.Length(),pco->iMiscBuffer.MaxLength());
		TTlvStruct<RPacketContext::TPcoId,RPacketContext::TPcoItemDataLength> tlv(pcoPtr,0);
		tlv.ResetCursorPos();

		TPtr8 imsFlagPtr(NULL, 0);
		TInt err = tlv.NextItemL(RPacketContext::EEtelPcktIMCNNetworkSubsystemSignallingFlag, imsFlagPtr);
		imcnFlag->SetImsSignallingIndicator(err == KErrNone);
		}

	}

//===========================================================
// Cancel Start or Stop
//===========================================================

DEFINE_SMELEMENT(TAwaitingDataClientStopOrCancel, NetStateMachine::MState, TContext)
TBool TAwaitingDataClientStopOrCancel::Accept()
    {
    CNodeActivityBase* startActivity = iContext.Node().FindActivityById(ECFActivityStartDataClient);
    if (startActivity)
        {
        //cancel start
        startActivity->Cancel(iContext);
        return EFalse;
        }

    CoreNetStates::TAwaitingDataClientStop state(iContext);
    return state.Accept();
    }

DEFINE_SMELEMENT(TCancelDataClientStartInPDP, NetStateMachine::MStateTransition, PDPSCprStates::TContext)
void TCancelDataClientStartInPDP::DoL()
    {
    CNodeActivityBase* startActivity = iContext.Node().FindActivityById(ECFActivityStartDataClient);
    if (startActivity)
        {
        //cancel start with KErrDisconnected
        startActivity->SetError(KErrDisconnected);
        startActivity->Cancel(iContext);
        }
    else
        {
        RClientInterface::OpenPostMessageClose(TNodeCtxId(ECFActivityStart, iContext.NodeId()), iContext.NodeId(), TEBase::TCancel().CRef());
        }
    }

} //namespace end
