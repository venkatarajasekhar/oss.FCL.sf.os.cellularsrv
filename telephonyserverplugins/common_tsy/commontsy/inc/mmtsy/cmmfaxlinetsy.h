/*
* Copyright (c) 2006-2009 Nokia Corporation and/or its subsidiary(-ies).
* All rights reserved.
* This component and the accompanying materials are made available
* under the terms of "Eclipse Public License v1.0"
* which accompanies this distribution, and is available
* at the URL "http://www.eclipse.org/legal/epl-v10.html".
*
* Initial Contributors:
* Nokia Corporation - initial contribution.
*
* Contributors:
*
* Description:
*
*/



#ifndef CMMFAXLINETSY_H
#define CMMFAXLINETSY_H

//  INCLUDES
#include "cmmlinetsy.h"

// CLASS DECLARATION

/**
 * CMmFaxLineTsy implements mode dependent Fax Line based functionality 
 * (defined by Symbian)
 */
NONSHARABLE_CLASS( CMmFaxLineTsy ) : public CMmLineTsy
    {
    public:  // Constructors and destructor
        
        /**
         * Two-phased constructor.
         * @param aMmPhone Pointer to the Phone object
         * @param aMode Line mode
         * @param aName Name of this line
         * @param aMessageManager Pointer to the MessageManager
         * @return CMmFaxLineTsy* Created line object 
         */
        static CMmFaxLineTsy* NewL( CMmPhoneTsy* aMmPhone, 
            RMobilePhone::TMobileService aMode, const TDesC& aName, 
            CMmMessageManagerBase* aMessageManager );
        
        /**
         * Destructor.
         */
        ~CMmFaxLineTsy();

        // Functions from base classes

        /**
         * This method is used for notifying a client about an incoming call.
         *          
         *
         * @param aDataPackage Package containing information about the 
         * incoming call
         */
        void CompleteNotifyIncomingCall( CMmDataPackage* aDataPackage );

        /**
         * This method is used for notifying a line of a call entering Dialling 
         * state
         *          
         *
         * @param aDataPackage Package containing information about the call
         */
        void CompleteNotifyDiallingStatus( CMmDataPackage* aDataPackage );

        /**
         * This function creates a new name for a call and opens a new call.
         *          
         *
         * @param aName Name of the new call.
         * @return CTelObject* Call object that was opened.
         */
        CTelObject* OpenNewObjectL( TDes& aNewName );

         /**
         * This function sets iLastIncomingCall to NULL.
         *          
         */
        void ResetLastIncomingCall();
        
    private:  // Constructors and destructor

        /**
         * C++ default constructor.
         */
        CMmFaxLineTsy();

    private:  // Functions from base classes

        /**
         * Creates a call object for the call that has been initiated bypassing 
         * TSY
         *          
         *
         * @param aCallId: Call id of the new call
         * @param aCallMode: Call mode
         * @param aCallStatus: Call status
         * @return Pointer to created call object
         */
        CMmCallTsy* CreateGhostCallObject(
            TInt aCallId, 
            RMobilePhone::TMobileService /*aCallMode*/,
            RMobileCall::TMobileCallStatus aCallStatus );

        /**
         * Initialises miscellaneous internal attributes
         *          
         *
         */
        void InitInternalAttributesL();

        /**
         * Creates and stores a Call object for incoming call
         *          
         *
         * @return TInt Success/failure value
         */
        TInt CreateCallObjectForIncomingCall();

    public:     // Data
    
        /**
         * For that a CMmCallTsy instance knows whether another         
         * CMmCallTsy has opened a CMmTsyFax object              
         */                 
        TBool iFaxOpened;
		
        /**
         * mmfax: so that OpenNewCall 'knows' which call to open!
         */                 
        CMmCallTsy* iLastIncomingFaxCall;

    };

#endif      // CMMFAXLINETSY_H
            
// End of File
