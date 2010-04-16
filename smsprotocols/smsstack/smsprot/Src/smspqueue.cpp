// Copyright (c) 2003-2009 Nokia Corporation and/or its subsidiary(-ies).
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
// Implements all classes involved in queuing deletes, writes and sends.
// 
//

/**
 @file
*/

#include "smspqueue.h"
#include "smspcomm.h"
#include "Gsmumsg.h"
#include "smspdel.h"
#include "smspsend.h"
#include "smspenum.h"
#include "SmsuTimer.h"

//
//
// CQueuedSmsMessage
//
//


/**
 *  Constructor
 *  
 *  @param aSmsMessage the queued SMS message.
 *  @param aObserver the observer to notify when the queued operation has completed.
 *  @param aOptions options applicable to the queued operation.
 *  
 */
CQueuedSmsMessage::CQueuedSmsMessage(CSmsMessage* aSmsMessage,MSmsMessageObserver& aObserver,TUint aOptions)
	:iSmsMessage(aSmsMessage)
	,iObserver(aObserver)
	,iOptions(aOptions)
	{
	} // CQueuedSmsMessage::CQueuedSmsMessage


/**
 *  Deletes the queued CSmsMessage object.
 *  
 */
CQueuedSmsMessage::~CQueuedSmsMessage()
	{
	delete iSmsMessage;
	} // CQueuedSmsMessage::~CQueuedSmsMessage

//
//
// CSmspMessageQueue
//
//

CSmspMessageQueue::~CSmspMessageQueue()
	{
	__ASSERT_DEBUG(iMessageQueue.IsEmpty(), SmspPanic(KSmspQueueNotEmpty));
	while (!iMessageQueue.IsEmpty())
		{
		CQueuedSmsMessage* queuedMessage = iMessageQueue.First();
		iMessageQueue.Remove(*queuedMessage);
		delete queuedMessage;
		}
	} // CSmspMessageQueue::~CSmspMessageQueue


void CSmspMessageQueue::DoRunL()
	{
	LOGSMSPROT1("CSmspMessageQueue::DoRunL()");

	CompleteFirst(iStatus.Int());
	Start();
	} // CSmspMessageQueue::DoRunL


void CSmspMessageQueue::Queue(CSmsMessage* aMessage, MSmsMessageObserver& aObserver, TUint aOptions)
	{
	TRAPD(err, DoQueueL(aMessage, aObserver, aOptions));

	LOGSMSPROT4("*** CSmspMessageQueue::Queue [err=%d aObserver=0x%08x IsActive=%d]", err, &aObserver, IsActive());

	if (err != KErrNone)
		{
		CompleteObserver(aObserver, err);
		}
	else if (!IsActive())
		{
		Start();
		}
	} // CSmspMessageQueue::Queue


void CSmspMessageQueue::DoQueueL(CSmsMessage* aMessage, MSmsMessageObserver& aObserver, TUint aOptions)
	{
	LOGSMSPROT1("CSmspMessageQueue::DoQueueL()");

	CleanupStack::PushL(aMessage);

	CQueuedSmsMessage* queuedsmsmessage= new (ELeave) CQueuedSmsMessage(aMessage, aObserver, aOptions);

	CleanupStack::Pop(aMessage);
	CleanupStack::PushL(queuedsmsmessage);

	iMessageQueue.AddLast(*queuedsmsmessage);

	CleanupStack::Pop(queuedsmsmessage);
	} // CSmspMessageQueue::DoQueueL


CSmspMessageQueue::CSmspMessageQueue(MSmsComm& aSmsComm, TInt aPriority)
: CSmspProtocolQueue(aSmsComm, aPriority)
	{
	iMessageQueue.SetOffset(_FOFF(CQueuedSmsMessage,iLink));
	} // CSmspMessageQueue::CSmspMessageQueue


/**
 *  Completes all items in the queue with KErrCancel
 */
void CSmspMessageQueue::DoCancel()
	{
	LOGSMSPROT1("*** CSmspMessageQueue::DoCancel");

	TSglQueIter<CQueuedSmsMessage> iter(iMessageQueue);
	CQueuedSmsMessage* queuedsmsmessage = iter;

	if ( queuedsmsmessage!=NULL )
		{
		CompleteRequest(queuedsmsmessage, KErrCancel);
		}
	} // CSmspMessageQueue::DoCancel


void CSmspMessageQueue::CompleteFirst(TInt aStatus)
	{
	LOGSMSPROT1("CSmspMessageQueue::CompleteFirst()");

	CompleteRequest(iMessageQueue.First(), aStatus);
	} // CSmspMessageQueue::CompleteFirst


void CSmspMessageQueue::CompleteRequest(CQueuedSmsMessage* aQueuedMessage, TInt aStatus)
	{
	LOGSMSPROT4("*** CSmspMessageQueue::CompleteRequest [aStatus=%d aObserver=0x%X IsFirst=%d]", aStatus, &aQueuedMessage->iObserver, iMessageQueue.IsFirst(aQueuedMessage));

	// From defect HOE-563KLY, need to cancel request if active and remove from queue
	// before observer has chance to manipulate queue
	CancelRequestIfObserved(aQueuedMessage->iObserver);
	iMessageQueue.Remove(*aQueuedMessage);
	CompleteObserver(aQueuedMessage->iObserver, aStatus, aQueuedMessage->iSmsMessage);
	delete aQueuedMessage;
	} // CSmspMessageQueue::CompleteRequest


void CSmspMessageQueue::CancelObserver(MSmsMessageObserver& aObserver)
	{
	LOGSMSPROT2("*** CSmspMessageQueue::CancelObserver [aObserver=0x%X]", &aObserver);

	TSglQueIter<CQueuedSmsMessage> iter(iMessageQueue);
	CQueuedSmsMessage* queuedsmsmessage = iter;

	for (; queuedsmsmessage!=NULL; queuedsmsmessage = iter++)
		{
		if (&queuedsmsmessage->iObserver==&aObserver)
			{
			if (iMessageQueue.IsFirst(queuedsmsmessage) && IsActive())
				{
				Cancel();
				}
			else
				{
				CompleteRequest(queuedsmsmessage, KErrCancel);
				}
			iter=iMessageQueue;
			}
		}

	if (!iMessageQueue.IsEmpty())
		Start();
	} // CSmspMessageQueue::CancelObserver


//
//
// CSmspProtocolQueue
//
//

CSmspProtocolQueue::CSmspProtocolQueue(MSmsComm& aSmsComm, TInt aPriority)
: CSmsuActiveBase(aPriority), iSmsComm(aSmsComm)
	{
	} // CSmspProtocolQueue::CSmspProtocolQueue


//
//
// CQueuedPDUDelete
//
//

/**
 *  2 phase constructor
 *  
 *  @param aSlotArray an array of message slots to delete from the phone.
 *  @param aObserver the observer to notify on completion.
 *  
 */
CQueuedPDUDelete* CQueuedPDUDelete::NewL(const CArrayFix<TGsmSmsSlotEntry>& aSlotArray,MSmsMessageObserver* aObserver)
	{
	LOGSMSPROT1("CQueuedPDUDelete::NewL()");

	CQueuedPDUDelete* queuedpdudelete=new(ELeave) CQueuedPDUDelete(aObserver);
	CleanupStack::PushL(queuedpdudelete);
	queuedpdudelete->ConstructL(aSlotArray);
	CleanupStack::Pop();
	return queuedpdudelete;
	} // CQueuedPDUDelete::NewL


CQueuedPDUDelete::~CQueuedPDUDelete()
    {
    } // CQueuedPDUDelete::~CQueuedPDUDelete


CQueuedPDUDelete::CQueuedPDUDelete(MSmsMessageObserver* aObserver)
    :iLocationArray(8)
    ,iObserver(aObserver)
    {
    } // CQueuedPDUDelete::CQueuedPDUDelete


/**
 *  2nd phase of construction, copies the elements of the slot array to
 *  the internal array.
 *  
 */
void CQueuedPDUDelete::ConstructL(const CArrayFix<TGsmSmsSlotEntry>& aSlotArray)
	{
	LOGSMSPROT1("CQueuedPDUDelete::ConstructL()");

	TInt count=aSlotArray.Count();
	for (TInt i=0; i<count; i++)
		iLocationArray.AppendL(aSlotArray[i]);
	} // CQueuedPDUDelete::ConstructL


//
//
// CSmspDeleteQueue
//
//

/**
 *  This method superceeds DoRunL(), implemented below.
 *  
 *  When the protocol stack is being closed down, the
 *  method iSmsComm.CloseNowWrap() deletes the stack and
 *  this active object. Calling CloseNowWrap() must be the
 *  last action performed by when the stack is being shut
 *  down, otherwise the action will be performed on a NULL
 *  object.
 *  
 *  @internalComponent
 *  @capability None
 */
void CSmspDeleteQueue::RunL()
	{
	LOGSMSPROT1("CSmspDeleteQueue::RunL()");

	CompleteFirst(iStatus.Int());

	if (iDeleteQueue.IsEmpty() && iSmsComm.ClosingDown())
		{
		iSmsComm.CloseNowWrap(); //causes CSmsProtocol to delete this
		//Don't do anything else in CSmspDeleteQueue because it is deleted now,
		//otherwise there will be an access violation!!
		}
	else
		{
		Start();
		}
	} // CSmspDeleteQueue::RunL


void CSmspDeleteQueue::DoRunL()
    {
    // Ignore in code coverage - the delete queue implements its own RunL.
    BULLSEYE_OFF
    LOGSMSPROT1("CSmspDeleteQueue::DoRunL()");

// This CSmspDeleteQueue::DoRunL() function would be called by
// CSmsuActiveBase::RunL().  However, CSmspDeleteQueue::RunL() now
// overwrites CSmsuActiveBase::RunL(), to make sure the delete queue is
// not used after the CloseNowWrap() call.  Therefore, this is now a
// do-nothing method.
    BULLSEYE_RESTORE
    }

void CSmspDeleteQueue::Queue(const CArrayFix<TGsmSmsSlotEntry>& aSlotArray, MSmsMessageObserver* aObserver)
	{
	TRAPD(err, DoQueueL(aSlotArray, aObserver));

	LOGSMSPROT4("*** CSmspDeleteQueue::Queue [err=%d aObserver=0x%X IsActive=%d]", err, aObserver, IsActive());

	if (err != KErrNone)
		{
		if (aObserver != NULL)
			{
			CompleteObserver(*aObserver, err);
			}
		}
	else if (!IsActive())
		Start();
	} // CSmspDeleteQueue::Queue


void CSmspDeleteQueue::DoQueueL(const CArrayFix<TGsmSmsSlotEntry>& aSlotArray, MSmsMessageObserver* aObserver)
	{
	LOGSMSPROT1("CSmspDeleteQueue::DoQueueL()");

	CQueuedPDUDelete* queuedDelete= CQueuedPDUDelete::NewL(aSlotArray, aObserver);
	iDeleteQueue.AddLast(*queuedDelete);
	} // CSmspDeleteQueue::DoQueueL


CSmspDeleteQueue::CSmspDeleteQueue(MSmsComm& aSmsComm, TInt aPriority)
: CSmspProtocolQueue(aSmsComm, aPriority)
	{
	LOGSMSPROT1("CSmspDeleteQueue::CSmspDeleteQueue()");

	iDeleteQueue.SetOffset(_FOFF(CQueuedPDUDelete,iLink));
	} // CSmspDeleteQueue::CSmspDeleteQueue


/**
 *  Completes all items in the queue with KErrCancel
 */
void CSmspDeleteQueue::DoCancel()
	{
	LOGSMSPROT1("*** CSmspDeleteQueue::DoCancel");

	iSmsPDUDelete->Cancel();

	while (!iDeleteQueue.IsEmpty())
		{
		CQueuedPDUDelete* queuedDelete = iDeleteQueue.First();
		CompleteRequest(queuedDelete, KErrCancel);
		}
	} // CSmspDeleteQueue::DoCancel


void CSmspDeleteQueue::CompleteFirst(TInt aStatus)
	{
	LOGSMSPROT1("CSmspDeleteQueue::CompleteFirst()");

	CompleteRequest(iDeleteQueue.First(), aStatus);
	} // CSmspDeleteQueue::CompleteFirst


void CSmspDeleteQueue::CompleteRequest(CQueuedPDUDelete* aQueuedDelete, TInt aStatus)
	{
	LOGSMSPROT4("*** CSmspDeleteQueue::CompleteRequest [aStatus=%d aObserver=0x%X IsFirst=%d]", aStatus, aQueuedDelete->iObserver, iDeleteQueue.IsFirst(aQueuedDelete));

	if (aQueuedDelete->iObserver != NULL)
		CompleteObserver(*aQueuedDelete->iObserver, aStatus);

	iDeleteQueue.Remove(*aQueuedDelete);
	delete aQueuedDelete;
	} // CSmspDeleteQueue::CompleteRequest


void CSmspDeleteQueue::CancelObserver(MSmsMessageObserver& aObserver)
	{
	LOGSMSPROT2("*** CSmspDeleteQueue::CancelObserver [aObserver=0x%X]", &aObserver);

	TSglQueIter<CQueuedPDUDelete> iter(iDeleteQueue);
	CQueuedPDUDelete* queuedDelete = iter;

	for (; queuedDelete!=NULL; queuedDelete = iter++)
		{
		if (queuedDelete->iObserver == &aObserver)
			{
			if (iDeleteQueue.IsFirst(queuedDelete) && IsActive())
				{
				Cancel();
				}
			else
				{
				CompleteRequest(queuedDelete, KErrCancel);
				}
			iter=iDeleteQueue;	// <---- Here is fix for crash
			}
		}
	} // CSmspDeleteQueue::CancelObserver


CSmspDeleteQueue* CSmspDeleteQueue::NewL(MSmsComm& aSmsComm, const TSmsSettings& aSmsSettings, RMobileSmsMessaging& aSmsMessaging, TInt aPriority)
	{
	LOGSMSPROT1("CSmspDeleteQueue::NewL()");

	CSmspDeleteQueue* self = new (ELeave) CSmspDeleteQueue(aSmsComm, aPriority);
	CleanupStack::PushL(self);
	self->ConstructL(aSmsSettings, aSmsMessaging);
	CleanupStack::Pop(self);
	return self;
	} // CSmspDeleteQueue::NewL


void CSmspDeleteQueue::Start()
	{
	LOGSMSPROT3("*** CSmspDeleteQueue::Start [IsActive=%d IsEmpty=%d]", IsActive(), iDeleteQueue.IsEmpty());

	if (!IsActive() && !iDeleteQueue.IsEmpty())
		{
		CQueuedPDUDelete& queuedDelete = *iDeleteQueue.First();
		iSmsPDUDelete->Start(queuedDelete.iLocationArray, iStatus);
		SetActive();
		}
	} // CSmspDeleteQueue::Start


CSmspDeleteQueue::~CSmspDeleteQueue()
	{
	Cancel();
	delete iSmsPDUDelete;

	__ASSERT_DEBUG(iDeleteQueue.IsEmpty(), SmspPanic(KSmspQueueNotEmpty));
	while (!iDeleteQueue.IsEmpty())
		{
		CQueuedPDUDelete* queuedDelete = iDeleteQueue.First();
		iDeleteQueue.Remove(*queuedDelete);
		delete queuedDelete;
		}
	} // CSmspDeleteQueue::~CSmspDeleteQueue


void CSmspDeleteQueue::ConstructL(const TSmsSettings& aSmsSettings, RMobileSmsMessaging& aSmsMessaging)
	{
	LOGSMSPROT1("CSmspDeleteQueue::ConstructL()");

	iSmsPDUDelete = CSmsPDUDelete::NewL(aSmsSettings, aSmsMessaging);
	} // CSmspDeleteQueue::ConstructL


void CSmspDeleteQueue::CompleteObserver(MSmsMessageObserver& aObserver, TInt aError, const CSmsMessage*)
	{
	LOGSMSPROT4("*** CSmspDeleteQueue::CompleteObserver [aObserver=0x%X aError=%d IsActive=%d]", &aObserver, aError, IsActive());

	if (iSmsComm.ObserverIsPresent(aObserver))
		{
		aObserver.MessageDeleteCompleted(aError);
		}
	} // CSmspDeleteQueue::CompleteObserver

//
//
// CSmspSendQueue
//
//

CSmspSendQueue* CSmspSendQueue::NewL(MSmsComm& aSmsComm, CSmsSegmentationStore& aSegmentationStore, const TSmsSettings& aSmsSettings, const RMobileSmsMessaging::TMobileSmsCapsV1& aMobileSmsCaps, RMobileSmsMessaging& aSmsMessaging, TInt aPriority, CSmspSetBearer& aSmspSetBearer)
	{
	LOGSMSPROT1("CSmspSendQueue::NewL()");

	CSmspSendQueue* self = new (ELeave) CSmspSendQueue(aSmsComm, aPriority);
	CleanupStack::PushL(self);
	self->ConstructL(aSegmentationStore, aSmsSettings, aMobileSmsCaps, aSmsMessaging, aSmspSetBearer);
	CleanupStack::Pop(self);
	return self;
	} // CSmspSendQueue::NewL


void CSmspSendQueue::Start()
	{
	LOGSMSPROT3("*** CSmspSendQueue::Start [IsActive=%d IsEmpty=%d]", IsActive(), iMessageQueue.IsEmpty());

	if (!IsActive() && !iMessageQueue.IsEmpty())
		{
		CQueuedSmsMessage& queuedMessage = *iMessageQueue.First();
		iSmsMessageSend->Start(*queuedMessage.iSmsMessage, queuedMessage.iOptions, queuedMessage.iObserver.GetLocalAddress(), iStatus);
		SetActive();
		}
	} // CSmspSendQueue::Start


CSmspSendQueue::CSmspSendQueue(MSmsComm& aSmsComm, TInt aPriority)
: CSmspMessageQueue(aSmsComm, aPriority)
	{
	} // CSmspSendQueue::CSmspSendQueue


CSmspSendQueue::~CSmspSendQueue()
	{
	Cancel();
	delete iSmsMessageSend;
	} // CSmspSendQueue::~CSmspSendQueue


void CSmspSendQueue::ConstructL(CSmsSegmentationStore& aSegmentationStore, const TSmsSettings& aSmsSettings, const RMobileSmsMessaging::TMobileSmsCapsV1& aMobileSmsCaps, RMobileSmsMessaging& aSmsMessaging, CSmspSetBearer& aSmspSetBearer)
	{
	LOGSMSPROT1("CSmspSendQueue::ConstructL()");

	iSmsMessageSend = CSmsMessageSend::NewL(aSegmentationStore, aSmsSettings, aMobileSmsCaps, aSmsMessaging, Priority(), aSmspSetBearer);
	} // CSmspSendQueue::ConstructL


void CSmspSendQueue::CancelRequestIfObserved(MSmsMessageObserver& aObserver)
	{
	LOGSMSPROT1("CSmspSendQueue::CancelRequestIfObserved()");

	if (IsActive() && &iMessageQueue.First()->iObserver == &aObserver)
		{
		iSmsMessageSend->Cancel();
		}
	} // CSmspSendQueue::CancelRequestIfObserved


void CSmspSendQueue::CompleteObserver(MSmsMessageObserver& aObserver, TInt aError, const CSmsMessage*)
	{
	LOGSMSPROT4("*** CSmspSendQueue::CompleteObserver [aObserver=0x%X aError=%d IsActive=%d]", &aObserver, aError, IsActive());

	if (iSmsComm.ObserverIsPresent(aObserver))
		{
		aObserver.MessageSendCompleted(aError);
		}
	} // CSmspSendQueue::CompleteObserver


void CSmspSendQueue::Complete(TInt aStatus)
	{
	LOGSMSPROT1("CSmspSendQueue::Complete()");

	iSmsComm.MessageSendCompleted(aStatus);
	} // CSmspSendQueue::Complete


//
//
// CSmspWriteQueue
//
//

CSmspWriteQueue* CSmspWriteQueue::NewL(MSmsComm& aSmsComm, const TSmsSettings& aSmsSettings, RMobilePhone& aGsmPhone, CSmsSegmentationStore& aSegmentationStore, TInt aPriority)
	{
	LOGSMSPROT1("CSmspWriteQueue::NewL()");

	CSmspWriteQueue* self = new (ELeave) CSmspWriteQueue(aSmsComm, aPriority);
	CleanupStack::PushL(self);
	self->ConstructL(aSmsSettings, aGsmPhone, aSegmentationStore);
	CleanupStack::Pop(self);
	return self;
	} // CSmspWriteQueue::NewL


void CSmspWriteQueue::Start()
	{
	LOGSMSPROT3("*** CSmspWriteQueue::Start [IsActive=%d IsEmpty=%d]", IsActive(), iMessageQueue.IsEmpty());

	if (!IsActive() && !iMessageQueue.IsEmpty())
		{
		CQueuedSmsMessage& queuedMessage = *iMessageQueue.First();
		iSmsMessageWrite->Start(queuedMessage.iSmsMessage, iStatus);
		SetActive();
		}
	} // CSmspWriteQueue::Start


CSmspWriteQueue::CSmspWriteQueue(MSmsComm& aSmsComm, TInt aPriority)
: CSmspMessageQueue(aSmsComm, aPriority)
	{
	} // CSmspWriteQueue::CSmspWriteQueue


CSmspWriteQueue::~CSmspWriteQueue()
	{
	Cancel();
	delete iSmsMessageWrite;
	} // CSmspWriteQueue::~CSmspWriteQueue


void CSmspWriteQueue::ConstructL(const TSmsSettings& aSmsSettings, RMobilePhone& aGsmPhone, CSmsSegmentationStore& aSegmentationStore)
	{
	LOGSMSPROT1("CSmspWriteQueue::ConstructL()");

	iSmsMessageWrite = CSmsMessageWrite::NewL(iSmsComm, aSmsSettings, aGsmPhone, aSegmentationStore);
	} // CSmspWriteQueue::ConstructL


void CSmspWriteQueue::CancelRequestIfObserved(MSmsMessageObserver& aObserver)
	{
	LOGSMSPROT1("CSmspWriteQueue::CancelRequestIfObserved()");

	if (IsActive() && &iMessageQueue.First()->iObserver == &aObserver)
		{
		iSmsMessageWrite->Cancel();
		}
	} // CSmspWriteQueue::CancelRequestIfObserved


void CSmspWriteQueue::CompleteObserver(MSmsMessageObserver& aObserver, TInt aError, const CSmsMessage* aSmsMessage)
	{
	LOGSMSPROT4("*** CSmspWriteQueue::CompleteObserver [aObserver=0x%X aError=%d IsActive=%d]", &aObserver, aError, IsActive());
	if (iSmsComm.ObserverIsPresent(aObserver))
		{
		aObserver.MessageWriteCompleted(aError, aSmsMessage);
		}
	} // CSmspWriteQueue::CompleteObserver


void CSmspWriteQueue::Complete(TInt)
	{
	LOGSMSPROT1("CSmspWriteQueue::Complete()");

	//Do Nothing :o)
	} // CSmspWriteQueue::Complete

