// Copyright (c) 2008-2009 Nokia Corporation and/or its subsidiary(-ies).
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
//

/**
 @file
 @internalComponent
*/

#ifndef SYMBIAN_PDPTIERMANAGER_H
#define SYMBIAN_PDPTIERMANAGER_H

#include <comms-infras/coretiermanager.h>
#include <comms-infras/contentionmanager.h>
#include <etelpckt.h>
using namespace ESock;

class CMBMSEngine;
class CContextStatusMonitor;
class CContextTypeChecker;


class MPacketServiceNotifier
	{
public:	
	virtual void PacketServiceAttachedCallbackL() = 0;
	};

class MContextEventsObserver
	{
public:
	virtual void PrimaryContextAddedL(const TName* aContextName) = 0;
	virtual void SecondaryContextAdded(const TName* aContextName) = 0;
	virtual void PrimaryContextDeleted(const CContextStatusMonitor* aContextStatusMonitor) = 0;
	virtual void ContextMonitoringError(const CContextStatusMonitor* aContextStatusMonitor, TInt aError) = 0;
	virtual void ContextTypeCheckingError(const TName* aContextName, TInt aError) = 0;
	};

class CContextStatusMonitor : public CActive
	{
public:
	CContextStatusMonitor(RPacketService& aPacketService, MContextEventsObserver& aCallback);
	~CContextStatusMonitor();

public:
	TBool StartL(const TName& aContextName);
	inline TBool IsPassedThroughActiveState() const;

private:
	void RunL();
	void DoCancel();
	TInt RunError(TInt aError);

private:
	RPacketService& iPacketService;
	RPacketContext iPacketContext;
	RPacketContext::TContextStatus iContextStatus;
	MContextEventsObserver& iCallback;
	TName iContextName;
	TBool iWasActive;
	};


class CPrimaryContextsMonitor : public CActive, MContextEventsObserver
	{
	enum TState {EEnumeratingContexts, EGettingInfo, EListening};
public:
	CPrimaryContextsMonitor(RPacketService& aPacketService, MContentionObserver& aCallback);
	~CPrimaryContextsMonitor();
public:
	void StartL();

// from MContextEventsObserver
	void PrimaryContextAddedL(const TName* aContextName);
	void SecondaryContextAdded(const TName* aContextName);
	void PrimaryContextDeleted(const CContextStatusMonitor* aContextStatusMonitor);
	void ContextMonitoringError(const CContextStatusMonitor* aContextStatusMonitor, TInt aError);
	void ContextTypeCheckingError(const TName* aContextName, TInt aError);

private:
	void RunL();
	void DoCancel();
	TInt RunError(TInt aError);
	void DeleteContextStatusMonitor(const CContextStatusMonitor* aContextStatusMonitor);
	void SwitchStateL();
	void StartContextStatusMonitoringL(const TName& aContextName);
	void RemoveContextNameAndCheckNext(const TName* aContextName);
	void ProcessError(TInt aError);

private:
	RPointerArray<CContextStatusMonitor> iContextMonitors;
	RPointerArray<TName> iAddedContextsNames;
	CContextTypeChecker* iContextTypeChecker;
	RPacketService& iPacketService;
	TState iState;
	RPacketService::TNifInfoV2 iCurrentNifInfo;
	TInt iInitialNifsCount;
	TInt iCurrentNifIndex;
	TBool iFirstContextAdded;
	MContentionObserver& iCallback;
	};

class CContextTypeChecker : public CActive
	{
public:
	CContextTypeChecker(RPacketService& aPacketService, MContextEventsObserver& aCallback);
	~CContextTypeChecker();

public:
	void Start(const TName* aContextName);
	void RunL();
	TInt RunError(TInt aError);
	void DoCancel();

private:
	RPacketService& iPacketService;
	const TName* iContextName;
	MContextEventsObserver& iCallback;
	TInt iCountInNif;
	};

class CPdpContentionManager : public CContentionManager
	{
public:
	static CPdpContentionManager* NewL(const ESock::CTierManagerBase& aTierManager, RPacketService& aPacketService);
	~CPdpContentionManager();

public:
	void StartMonitoringL();

private:
	void ConstructL(RPacketService& aPacketService);
	CPdpContentionManager(const ESock::CTierManagerBase& aTierManager);

private:
	// from CContentionManager
	virtual void ContentionResolved(const TContentionRequestItem& aContentionRequest, TBool aResult);
	virtual void ContentionOccured(ESock::CMetaConnectionProviderBase& aMcpr);
	virtual void ReportContentionAvailabilityStatus(ESock::CMetaConnectionProviderBase& aMcpr, const ESock::TAvailabilityStatus& aStatus) const;
	
private:
	CPrimaryContextsMonitor* iPrimaryContextsMonitor;
	};

//PDP Tier Manager
class CPDPTierManager : public CCoreTierManager, public MPacketServiceNotifier
	{
public:
	static CPDPTierManager* NewL(ESock::CTierManagerFactoryBase& aFactory);
	~CPDPTierManager();

public:
	void PacketServiceAttachedCallbackL();
	TBool IsUnavailableDueToContention(const ESock::CMetaConnectionProviderBase* aMetaConnectionProvider) const;

protected:
    CPDPTierManager(ESock::CTierManagerFactoryBase& aFactory, const MeshMachine::TNodeActivityMap& aActivityMap);

    virtual ESock::MProviderSelector* DoCreateProviderSelectorL(const Meta::SMetaData& aSelectionPreferences);
	virtual void ReceivedL(const Messages::TRuntimeCtxId& aSender, const Messages::TNodeId& aRecipient, Messages::TSignatureBase& aMessage);

	TBool HandleContentionL(ESock::CMetaConnectionProviderBase* aMcpr, Messages::TNodeId& aPendingCprId, TUint aPriority);
private:
    void ConstructL();

private:
	CMBMSEngine* iMBMSEngine;
	CPdpContentionManager* iPdpContentionManager;
	};

inline TBool CContextStatusMonitor::IsPassedThroughActiveState() const
	{
	return iWasActive;
	}

#endif // SYMBIAN_PDPTIERMANAGER_H

