// Copyright (c) 2004-2010 Nokia Corporation and/or its subsidiary(-ies).
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
// Implements the interface to BCA.
// 
//

/**
 @file BcaIoController.cpp
*/

#include <e32uid.h>
#include <nifmbuf.h>
#include <e32svr.h>
#include <u32hal.h>

#include "Constants.h"
#include "BcaIoController.h"
#include "Sender.h"
#include "Receiver.h"

const TUint KDefaultSendQueueSize=5;

#ifdef __EABI__
// Patch data is used and KMaxTxIPPacketSize and KMaxRxIPPacketSize can be modified to a different value in RawIpNif.iby file
extern const TInt KMaxSendQueueLen = KDefaultSendQueueSize;
extern const TInt KMaxTxIPPacketSize = KMaxIPPacket + KIPTagHeaderLength;
extern const TInt KMaxRxIPPacketSize = KMaxIPPacket + KIPTagHeaderLength;
#endif

CBcaIoController::CBcaIoController(MControllerObserver& aObserver,
	CBttLogger* aTheLogger)
/**
 * Constructor. 
 *
 * @param aObserver Reference to the observer of this state machine
 * @param aTheLogger The logging object
 */
    : iTheLogger(aTheLogger),
      iSendState(EIdle),
      iFlowBlocked(EFalse),
      iNumPacketsInSendQueue(0),
      iObserver(aObserver),
      iMBca(NULL),
      iSender(NULL),
      iReceiver(NULL),
      iLoader(NULL)	  
    {
    }

CBcaIoController* CBcaIoController::NewL(MControllerObserver& aObserver, CBttLogger* aTheLogger)
/**
 * Two-phase constructor. Creates a new CBcaIoController object, performs 
 * second-phase construction, then returns it.
 *
 * @param aObserver The observer, to which events will be reported
 * @param aTheLogger The logging object
 * @return A newly constructed CBcaIoController object
 */
    {
    CBcaIoController* self = new (ELeave) CBcaIoController(aObserver, aTheLogger);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

void CBcaIoController::ConstructL()
/**
 * Second-phase constructor. Creates all the state objects it owns.
 */
    {
    _LOG_L1C1(_L8("CBcaIoController::ConstructL"));

#ifdef RAWIP_HEADER_APPENDED_TO_PACKETS
    iIPTagHeader = new (ELeave) CIPTagHeader(iTheLogger);
#endif // RAWIP_HEADER_APPENDED_TO_PACKETS

#if defined __EABI__
    // Default value for queue length
    iMaxSendQueueLen = KMaxSendQueueLen;
    // Default value for Tx and Rx packet size
    iMaxTxPacketSize = KMaxTxIPPacketSize;
    iMaxRxPacketSize = KMaxRxIPPacketSize;
#else // WINS
    // Set default values in case patch is not present in epocrawip.ini
    iMaxSendQueueLen = KDefaultSendQueueSize;    
    iMaxTxPacketSize = KMaxIPPacket + KIPTagHeaderLength;
    iMaxRxPacketSize = KMaxIPPacket + KIPTagHeaderLength;
           
    // for the emulator process is patched via the epocrawip.ini file
    UserSvr::HalFunction(EHalGroupEmulator,EEmulatorHalIntProperty,(TAny*)"rawip_KMaxSendQueueLen",&iMaxSendQueueLen);
    UserSvr::HalFunction(EHalGroupEmulator,EEmulatorHalIntProperty,(TAny*)"rawip_KMaxTxIPPacketSize",&iMaxTxPacketSize);
    UserSvr::HalFunction(EHalGroupEmulator,EEmulatorHalIntProperty,(TAny*)"rawip_KMaxRxIPPacketSize",&iMaxRxPacketSize);
#endif
    
    // end note
    
    iSender = CSender::NewL(*this, iTheLogger, iMaxTxPacketSize);
    iReceiver = CReceiver::NewL(*this, iTheLogger, iMaxRxPacketSize);
    iLoader = new (ELeave) CBcaControl(*this, iTheLogger);
    }


CBcaIoController::~CBcaIoController()
/**
 * Destructor.
 */
	{
    iSendQueue.Free();
    iNumPacketsInSendQueue = 0;
    
	delete iReceiver;
	delete iSender;
	delete iLoader;
	
#ifdef RAWIP_HEADER_APPENDED_TO_PACKETS
    delete iIPTagHeader;
#endif // RAWIP_HEADER_APPENDED_TO_PACKETS
	}

/** sets the BCA Stack name

* @param aBcaStack Text composed of bca stack and next bca names
*/	
void CBcaIoController::SetBcaStackAndName(const TDesC& aBcaStack, const TDesC& aBcaName)
	{
	iBcaName.Set(aBcaName);
	iBcaStack.Set(aBcaStack);
	}
	

void CBcaIoController::StartL()
/**
 *  Used to kick off the initialisation for this module
 */
	{
	_LOG_L1C1(_L8("CBcaIoController::StartL is called."));

    iLoader->StartLoadL();
	}

void CBcaIoController::Stop(TInt aError)
/**
 *  Used to shutdown this module. This will cancel all the outstanding 
 *  requests on the active objects owned by this module and shutdown.
 * @param aError the passed in error code as to why Stop has been called
 */
	{
	_LOG_L1C1(_L8("CBcaIoController::Stop is called."));

	//Stop all the active objects
	iReceiver->Cancel();

	if (iSendState == ESending)
		{
		iSender->Cancel();
		}

	// Update module state
	SendState(EShuttingDown);
	
    iLoader->ShutdownBca(aError);  
	}


ESock::MLowerDataSender::TSendResult CBcaIoController::Send(RMBufChain& aPdu)
/**
 *  This method is called by CRawIPFlow in order to send a packet down
 *  to the BCA. 
 *
 *  @param aPdu a data packet
 */
    {
    _LOG_L1C1(_L8(">>CBcaIoController::Send"));

    // Check if flow is shutting down
    if (iSendState == EShuttingDown)
        {
        _LOG_L2C1(_L8("    ERROR: Nif is shutting down"));
        
        // when the flow is destroyed the memory for this packet will be 
        // cleaned up - just tell the layers above to stop sending.
        aPdu.Free();
        
        return ESock::MLowerDataSender::ESendBlocked;
        }
    
    // check that this packet isnt too big - If it is, we dont want to send it or
    // add it to our queue
    if ((aPdu.Length() - aPdu.First()->Length()) > iMaxTxPacketSize)
        {
        _LOG_L2C1(_L8("Packet is too large - discarding"));
        _LOG_L1C1(_L8("<<CSender::Send -> Error"));

        // in debug panic - this should not happen, MTU on the uplink should
        // be strictly enforced
        __ASSERT_DEBUG(ETrue,Panic(KFlowInvalidULPacketSize));
        
        aPdu.Free();
       
        // may be counter intuitive, however the only options here are either 
        // send accepted or blocked (MLowerDataSender).
        
        _LOG_L2C1(_L8("<<CBcaIoController::Send - return ContinueSending"));
        return ESock::MLowerDataSender::ESendAccepted;
        }
    
    // transmit flow control.
    if (iFlowBlocked)
        {
        // Transmit is off for this flow - we must have received a block
        // message from our control.  append this message to the queue
        // and tell the layer above it to kindly stop sending.
        _LOG_L1C1(_L8("    Sender blocked, appending packet to queue"));
        
        AppendToSendQueue(aPdu);
        
        // may be a bit counter-intuitive, however, even if the flow is blocked
        // there is no reason not to ask for more data as long as our send
        // queue isn't full.
        
        if (IsSendQueueFull())
            {
            _LOG_L2C1(_L8("<<CBcaIoController::Send - return StopSending"));
            return ESock::MLowerDataSender::ESendBlocked;
            }
        else
            {
            _LOG_L2C1(_L8("<<CBcaIoController::Send - return ContinueSending"));
            return ESock::MLowerDataSender::ESendAccepted;       
            }
        }
    
    // transmit is on for this flow.  send a packet or queue it
    // if we're already sending one.

    if (iSendState == ESending)
        // If this happens, it means that TCP/IP has sent us an IP packet
        // while we're still sending the previous one. 
        {    
        _LOG_L1C1(_L8("    Sender busy, appending packet to queue"));
        AppendToSendQueue(aPdu);
        
        if (IsSendQueueFull())
            {
            _LOG_L2C1(_L8("<<CBcaIoController::Send - return StopSending"));
            return ESock::MLowerDataSender::ESendBlocked;
            }
        }
    else
        {
        // if we're idle, then we must not be sending.  If we're not idle
        // then the SendComplete() will handle sending any additional 
        // packets that might have been queued onto the send queue.
    
        // Update module state
        _LOG_L2C1(_L8("     set State to ESending"));
        iSendState = ESending;
         
        iSender->Send(aPdu);
        }

    // if our send queue isn't full, then the send is accepted
    // otherwise, block this flow until we have room for the next
    // packet
    
    _LOG_L2C1(_L8("<<CBcaIoController::Send - return ContinueSending"));

    return ESock::MLowerDataSender::ESendAccepted;
    }
	
void CBcaIoController::SendComplete()
/**
 *  A packet has finished sending - check to see if we need
 *  to process more packets.
 */
    {
    _LOG_L1C1(_L8(">>CBcaIoController::SendComplete"));

    // if we've been blocked while in the middle of a 
    // send - don't continue sending, this will happen
    // when the flow is resumed.

    iSendState = EIdle;

    // are we available to transmit?
    
    if (iFlowBlocked == EFalse)
        {
        // flow transmit is on
    
        TBool resumeSending = EFalse;
    
        // if the queue is full, it means that while a packet was
        // outstanding in BCA, we've filled up our send queue and
        // informed the upper layers to stop sending.
        
        if (IsSendQueueFull())
            {
            resumeSending = ETrue;
            }
        
        // if the queue has anything inside it, we need to send the 
        // first packet in the queue.  note: the resumeSending check is to 
        // short circuit this so we don't check the queue length twice
        // in the case that it is full
        
        if ((resumeSending) || (!IsSendQueueEmpty()))
            {
            iSendState = ESending;
            
            RMBufChain tmpPdu;
            _LOG_L1C1(_L8("    Packet removed from queue to send"));
            RemoveFromSendQueue(tmpPdu);
            
            // Update module state
            _LOG_L2C1(_L8("     set State to ESending"));
          
            iSender->Send(tmpPdu);
            
            // if the queue was full, again, we told the upper layers
            // to stop sending, we need to resume this flow.  order is
            // important as we don't want to get a packet before we've 
            // given one to BCA to send.
            if (resumeSending)
                {
                iObserver.ResumeSending();
                }
            }
        }
    
    _LOG_L1C1(_L8("<<CBcaIoController::SendComplete"));
    }


void CBcaIoController::ResumeSending()
/**
 *  Flow is being unblocked this will resume sending.
 */
    {
    _LOG_L1C1(_L8(">>CBcaIoController::ResumeSending"));

    // allows for normal SendComplete behaviour if there is
    // a packet outstanding with BCA
    iFlowBlocked = EFalse;
    
    // If the send state is idle, then there isn't a packet
    // outstanding, check the send queue to see if there are
    // packets to send.
    
    if (iSendState == EIdle)
        {
        // flow transmit is on
    
        TBool resumeSending = EFalse; 
        
        if (IsSendQueueFull())
            {
            resumeSending = ETrue;
            }
    
        // if the queue has anything inside it, we need to send the 
        // first packet in the queue.  note: the resumeSending check is to 
        // short circuit this so we don't check the queue length twice
        // in the case that it is full
        
        if ((resumeSending) || (!IsSendQueueEmpty()))
            {
            RMBufChain tmpPdu;
            _LOG_L1C1(_L8("    Packet removed from queue to send"));
            RemoveFromSendQueue(tmpPdu);
            
            // Update module state
            _LOG_L2C1(_L8("     set State to ESending"));
            iSendState = ESending;
          
            iSender->Send(tmpPdu);
            
            // if the queue was full, again, we told the upper layers
            // to stop sending, we need to resume this flow.  order is
            // important as we don't want to get a packet before we've 
            // given one to BCA to send.
            if (resumeSending)
                {
                iObserver.ResumeSending();
                }
            }
        }
    
    _LOG_L1C1(_L8("<<CBcaIoController::ResumeSending"));
    }

#ifdef RAWIP_HEADER_APPENDED_TO_PACKETS
void CBcaIoController::SetType(TUint16 aType)
    {
/**
 *  Used to specify the type of the IP header.
 */
    _LOG_L1C1(_L8("CBcaController::SetType"));
    
    iIPTagHeader->SetType(aType);   
    }

void CBcaIoController::AddHeader(TDes8& aDes)
/**
 *  Used to add the IP header to the packet before sending to the BCA.
 */
    {
    _LOG_L1C1(_L8("CBcaController::AddHeader"));

    iIPTagHeader->AddHeader(aDes);
    }

TUint16 CBcaIoController::RemoveHeader(RMBufChain& aPdu)
/**
 *  Used to remove the IP header from the received the packet before sending to the 
 *  TCP/IP layer.  
 * @return The IP header that has been removed from the packet
 */
    {
    _LOG_L1C1(_L8("CBcaController::RemoveHeader"));

    return (iIPTagHeader->RemoveHeader(aPdu));
    }   
#endif // RAWIP_HEADER_APPENDED_TO_PACKETS


CBcaControl::CBcaControl(CBcaIoController& aObserver, CBttLogger* aTheLogger)
/**
 * Constructor. Performs standard active object initialisation.
 *
 * @param aObserver Reference to the observer of this state machine
 * @param aTheLogger The logging object
 */
	: CActive(EPriorityStandard), 
	  iObserver(aObserver), 
	  iTheLogger(aTheLogger),
	  iMBca(NULL),
	  iState(EIdling),
	  iError(KErrNone)
	  
	{
	CActiveScheduler::Add(this);
	}
	
CBcaControl::~CBcaControl()
/**
 * Destructor.
 */
	{
	Cancel();
	if(iMBca)
		{
        //If the Bca is still open, close it
        if(EBcaOpened == iState)
            {
            iMBca->Close();
            }
        //delete the BCA instance
		iMBca->Release();	
		}
	// Library will be Closed when iBcaDll is destroyed.
	}

void CBcaControl::RunL()
/**
 *  Called after request is completed. 
 *  
 */
	{
	_LOG_L1C1(_L8("CBcaControl::RunL() called"));
	switch (iState)
		{
		//in this state, Ioctl is called to set IAP ID, check the result of
		// Ioctl, then either set the BCA stack with another Ioctl call, 
		// open the BCA (if there's no BCA stack to set), or stop the NIF.
		case EIdling:
			{
			if(iStatus == KErrNone || iStatus == KErrNotSupported)
				{
				if(iStatus == KErrNotSupported)
					{
					_LOG_L1C1(_L8("This BCA does not support IAPID set"));
					}
				else
					{
					_LOG_L2C1(_L8("This BCA supports IAPID set"));
					}
				
				TPtrC bcaStack = iObserver.BcaStack();
				if(bcaStack.Length())
					{
					TBuf8<KMaxName> remainingBcaStack8;
					remainingBcaStack8.Copy(bcaStack);
					iMBca->Ioctl(iStatus, KBcaOptLevelGeneric,KBCASetBcaStack,remainingBcaStack8);
					}
				else
					{
					TRequestStatus* statusPtr=&iStatus;
					User::RequestComplete(statusPtr,KErrNone);
					}
				iState = EIAPSet;
				SetActive();	
				}
			else
				{
				_LOG_L1C2(_L8("ERROR in BCA IAPID set = %d"), iStatus.Int());
				iObserver.Stop(iStatus.Int());
				}
			
			break;
			}
			
		//in this case, we receive the result of Ioctl call to set Bca Stack.
		// Check the result of Ioctl, then Open the Bca or stop the NIF
		case EIAPSet:
			{
			if(iStatus == KErrNotSupported || iStatus == KErrNone)
				{
				if(iStatus == KErrNotSupported)
					{
					_LOG_L1C1(_L8("This BCA does not support BCA stacking"));
					}
				else
					{
					_LOG_L2C1(_L8("This BCA supports BCA stacking"));
					}
				iMBca->Open(iStatus, iObserver.Port());
				iState = EBcaStackSet;
				SetActive();	
				}
			else
				{
				_LOG_L2C2(_L8("ERROR in BCA stack set = %d"), iStatus.Int());
				iObserver.Stop(iStatus.Int());
				}
			break;
			}
		
		//in this state, BCA Open is called. Checks the result of Open.
		// If it is successful,then start the NIF. Otherwise stops the NIF.
		case EBcaStackSet:
			{
			if(iStatus != KErrNone && iStatus !=  KErrAlreadyExists)
				{
				_LOG_L2C2(_L8("ERROR in BCA Open = %d"), iStatus.Int());
				iObserver.Stop(iStatus.Int());
				}
			else
				{
                iState = EBcaOpened;
                //Activate the receiver Active Object
				iObserver.Receiver().StartListening();
				_LOG_L1C1(_L8("CBcaIoController Is Initialised"));
				TRAPD(err, iObserver.GetObserver().InitialiseL(MRawIPObserverBase::EBcaController,KErrNone));
				if(err != KErrNone)
					{
					_LOG_L2C2(_L8("ERROR in BCA Open Initialise observer = %d"), err);
					iObserver.Stop(err);
					}
				}
			break;
			}

		//in this state, BCA is Shutdown, shutdown the NIF.
		case EClosing:
			{
			// linklayer shutdown
			iState = EIdling;
			iObserver.GetObserver().ShutDown(MControllerObserver::EBcaController, iError);
			break;
			}
		// Wrong state.
		default:
			{
			_LOG_L1C1(_L8("ERROR CBcaControl::RunL(): Unknown state"));
			_BTT_PANIC(KNifName, KBcaUnkownState);
			break;
			}
		}

	}
	
void CBcaControl::DoCancel()
/**
 *	cancel active request. 
 */
	{
	_LOG_L1C1(_L8("CBcaControl::DoCancel called."));
	_LOG_L2C2(_L8("iState value is %d"), iState);
	switch (iState)
		{
		case EIdling:
		case EIAPSet:
	    case EBcaStackSet:
			if(iMBca)
				{
				iMBca->CancelIoctl();
				}
			iState = EIdling;
			break;
		case EClosing:
            iState = EIdling;		    
            break;    
		default:
			_LOG_L2C1(_L8("ERROR CBcaControl::DoCancel(): Unknown state"));
			_BTT_PANIC(KNifName, KBcaUnkownState);
			break;
		}
	}
	
void CBcaControl::StartLoadL()
/**
 *  This method loads the C32BCA library and uses Ioctl to set the Bca iIapId. 
 */
	{
	_LOG_L1C1(_L8("CBcaControl::StartLoad"));
	
	//iMBca should not be initialized at this point
	__ASSERT_DEBUG(!iMBca,Panic(KBcaAlreadyExists));
	
	//We don't expect iMBca here, but if it occurs, we delete previous BCA Instance
	if(iMBca)
	    {
        //If the state is still "open", close it first
        if(EBcaOpened == iState)
            {
            iMBca->Close();
            }
        iMBca->Release();
        }

	// Loads Bca Dll and creates a Bca instance;
	User::LeaveIfError(iBcaDll.iObj.Load(iObserver.BcaName()));
	
	TNewBcaFactoryL newBcaFactoryProcL = (TNewBcaFactoryL)iBcaDll.iObj.Lookup(1);
	if (NULL == newBcaFactoryProcL)
		{
		_LOG_L1C2(_L8("Library entry point found error %d"), KErrBadLibraryEntryPoint);
		User::Leave(KErrBadLibraryEntryPoint);	
		}
	
	MBcaFactory* bcaFactory = (*newBcaFactoryProcL)();

	if(!bcaFactory)
		{
		_LOG_L1C2(_L8("BcaFactory creation error %d"), KErrCompletion);
		User::Leave(KErrCompletion);	
		}
	CleanupReleasePushL(*bcaFactory);
	
	iMBca = bcaFactory->NewBcaL();
	CleanupStack::PopAndDestroy(bcaFactory);
	
	iObserver.SetBca(iMBca); //Pass BCA pointer.

	TPckg<TUint32> aOpt(iObserver.IapId());
	iMBca->Ioctl(iStatus,KBcaOptLevelGeneric,KBCASetIapId,aOpt);
	
	iState = EIdling;
	SetActive();
	}

	
void CBcaControl::ShutdownBca(TInt aError)
/**
 *  Bca Shutdown.
 
 *  @param aError the error code to shutdown the NIF. 
 */
	{
	__ASSERT_DEBUG(iMBca,Panic(KBcaNotExist));
	Cancel();
        
    //We should only call shutdown or close if we have successfully opened a BCA Channel
    if((iMBca) && (EBcaOpened == iState))
        {
        if(aError == KErrConnectionTerminated )
            {
            _LOG_L1C1(_L8("This is an emergency shutdown, it kills the NIF immediately."));
            // It is a emergency shutdown, it kills the NIF immediately.
            iMBca->Close();
            iState = EIdling;
            iObserver.GetObserver().ShutDown(MControllerObserver::EBcaController, aError);
            }
        else
            {
            _LOG_L1C1(_L8("This is a graceful termination which takes a while."));
            //It is a graceful termination which takes a while.
            iError = aError;
            iState = EClosing;
            iMBca->Shutdown(iStatus);
            SetActive();    
            }
        }
    else //nothing to shutdown, just notify linklayer down.
        {
        _LOG_L1C1(_L8("Bca is not initialized or opened, bring the linklayer down"));
        iState = EIdling;
        iObserver.GetObserver().ShutDown(MControllerObserver::EBcaController, aError);
        }
  
	}

/** Panic function for RawIpNif 

* @param aPanic panic code */
void Panic(TRawIPNifPanic aPanic)
	{
	User::Panic(KNifName,aPanic);
	}

