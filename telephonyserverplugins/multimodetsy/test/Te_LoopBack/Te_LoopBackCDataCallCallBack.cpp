// Copyright (c) 1997-2009 Nokia Corporation and/or its subsidiary(-ies).
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
// This file initiates the "Data Call dial-up networking call-back" scenario for 
// the GSM Test Harness.  This test is setup to make an outgoing data-call that transfers
// data and then is terminated from the remote end.  The test then quickly responds to 
// an incoming call.  Data is received and the call is terminated again from the remote end.
// The client (application) side must start the emulator and drive the ETel API.  The 
// emulator side runs the designated script.
// 
//

/**
 @file
 @note There are mulitple classes implemented in this file.
 @note These classes are CTestDriveDataCallCallBack and CTestDataCallCallBack.
*/

#include <e32test.h>
#include "Te_LoopBackCDataCallCallBack.h"
#include "../../hayes/TSYCONFG.H" // for KInternetAccessPoint

//
// Test-side class
// With the assistance of the base class, this class must start the emulator
// and drive the ETel API.
//
CTestDriveDataCallCallBack* CTestDriveDataCallCallBack::NewL(const TScriptList aScriptListEntry,
													         TInt aVarDelay)
/**
 * 2 Phase Constructor
 *
 * This method creates an instance of CTestDriveDataCallCallBack.  The ConstructL for
 * for CTestDriveDataCallCallBack is inherited from and implemented in CTestBase. The 
 * ConstructL uses the CTestDriveDataCallCallBack object to load ond open the two iPhone 
 * objects, one from each server. 
 *
 * @param aScriptListEntry: enum indicating which script to use for this test.
 * @param aVarDelay: integer indicating variable delay value, in seconds, to be used with an EWait script
 * @leave Leaves if a failure occurs during connect or open of the iPhone by ConstructL.
 * @return pointer to the instance of CTestDriveDataCallCallBack.
 */
	{
	CTestDriveDataCallCallBack* aA=new(ELeave) CTestDriveDataCallCallBack(aScriptListEntry, aVarDelay);
	CleanupStack::PushL(aA);
	aA->ConstructL();
	CleanupStack::Pop();
	return aA;
	}


CTestDriveDataCallCallBack::CTestDriveDataCallCallBack(const TScriptList aScriptListEntry,
												       TInt aVarDelay
													  ) : iScriptListEntry(aScriptListEntry), 
														  iVarDelay(aVarDelay)
/**
 * This method is the constructor for CTestDriveDataCallCallBack.
 *
 * @param aScriptListEntry: enum indicating which script to use for this test.
 * @param aVarDelay: integer indicating variable delay value, in seconds, to be used with an EWait script
 * @note Initializes private data "aScriptListEntry" to received parameter.
 * @note Initializes private data "aVarDelay" to received parameter.
 */
	{}

TInt CTestDriveDataCallCallBack::RunTestL()
/**
 * This method is invoked to start a "Data Call dial-up networking call-back" test. 
 * This method sets the CTestBase current script to run and the CTestBase variable delay value to use for an EWait 
 * script and then starts the modem emulator side of the test.
 *
 * @return KErrNone when no error exists.
 * @return KErrAlreadyExists is returned if modem emulator exists and retry limit expires.
 * @return Variable depending on return value from test's DriveETelApiL method and thread monitoring.
 */
	{
	iCurrentScript=iScriptListEntry;
	iVariableDelay=iVarDelay;
	return StartEmulatorL();
	}

TInt CTestDriveDataCallCallBack::DriveETelApiL()
/**
 * This method contains the real meat of the Client-side "Data Call dial-up networking 
 * call-back" test code.  This method sets up an outgoing data-call write test that is 
 * terminated from the remote end.  The test then quickly responds to an incoming call.  
 * Data is received and the call is terminated again from the remote end.
 */
	{
	_LIT(KDataLineName,"Data");

	INFO_PRINTF1(_L("Opening Mobile Phone\n"));

	RLine line;
	INFO_PRINTF1(_L("Opening Data Line\n"));
	TESTL(line.Open(iPhone,KDataLineName)==KErrNone);

	INFO_PRINTF1(_L("Opening New Data Call\n"));
	RCall call;
	TESTL(call.OpenNewCall(line)==KErrNone);

	TRequestStatus reqStatus;
	RMobilePhone::TMMTableSettings tableSettings;
	tableSettings.iLocId=KInternetAccessPoint;
	RMobilePhone::TMMTableSettingsPckg tableSettingsPckg(tableSettings);
	iPhone.InitialiseMM(reqStatus , tableSettingsPckg); 	
	User::WaitForRequest(reqStatus);
	TESTL(reqStatus == KErrNone);

	// dial
	_LIT(KDialString,"+1234");
	TESTL(call.Dial(KDialString)==KErrNone);

	INFO_PRINTF1(_L("Loan Data Call Port to Comm Server\n"));
	RCall::TCommPort commPort;
	TESTL(call.LoanDataPort(commPort)==KErrNone);

	RCommServ cs;
	TESTL(cs.Connect()==KErrNone);

	RComm port;
	TESTL(port.Open(cs,commPort.iPort,ECommShared)==KErrNone);

	// Transfer data
	TRequestStatus stat;
	port.Write(stat,KWriteTestCallBackData);
	User::WaitForRequest(stat);
	TESTL(stat.Int()==KErrNone);

    //-- a small delay between successive writes to the COM port
    //-- I had to insert it to fix mistiming between script execution and sending AT-commands to modem
    User::After(500000);		

	port.Write(stat,KWriteTestCallBackData);
	User::WaitForRequest(stat);
	TESTL(stat.Int()==KErrNone);

	// Remote termination of call should have occurred in scripts, 
	// close port and comm server
	port.Close();
	cs.Close();
	INFO_PRINTF1(_L("Reclaim Data Call Port from Comm Server\n"));
	TESTL(call.RecoverDataPort()==KErrNone);

	// Now wait for an incoming call...
	INFO_PRINTF1(_L("Wait to Answer incoming Data Call\n"));
	TESTL(call.AnswerIncomingCall()==KErrNone);

	INFO_PRINTF1(_L("Loan Data Call Port to Comm Server\n"));
	TESTL(call.LoanDataPort(commPort)==KErrNone);

	TESTL(cs.Connect()==KErrNone);
	TESTL(port.Open(cs,commPort.iPort,ECommShared)==KErrNone);

	port.Write(stat,KWriteTestCallBackData2);
	User::WaitForRequest(stat);
	TESTL(stat.Int()==KErrNone);

    //-- a small delay between successive writes to the COM port
    //-- I had to insert it to fix mistiming between script execution and sending AT-commands to modem
    User::After(500000);		

	port.Write(stat,KWriteTestCallBackData2);
	User::WaitForRequest(stat);
	TESTL(stat.Int()==KErrNone);

	// Remote termination of call should have occurred in scripts, 
	// close port and comm server
	port.Close();
	cs.Close();
	INFO_PRINTF1(_L("Reclaim Data Call Port from Comm Server\n"));
	TESTL(call.RecoverDataPort()==KErrNone);

	TInt32 ss;
	TInt8 bar;
	iPhone.GetSignalStrength(stat,ss,bar);
	//	mmPhone.GetSignalStrength(stat,ss,bar);
	User::WaitForRequest(stat);
	TESTL(stat==KErrNone);

	// close iPhone, line and call
	line.Close();
	call.Close();
	return KErrNone;
	}

//
// Emulator-side class
// With the assistance of the base class, this class must run the designated script
//
CTestDataCallCallBack* CTestDataCallCallBack::NewL(const TScript* aScript)
/**
 * 2 Phase Constructor
 *
 * This method creates an instance of CTestDataCallCallBack.
 *
 * @param aScript: pointer to the specifics of the script to run. 
 * @leave Leaves if out of memory when attempting to create. 
 * @return pointer to the instance of CTestDataCallCallBack.
 */
	{
	CTestDataCallCallBack* aA=new(ELeave) CTestDataCallCallBack(aScript);
	CleanupStack::PushL(aA);
	aA->ConstructL();
	CleanupStack::Pop();
	return aA;
	}

CTestDataCallCallBack* CTestDataCallCallBack::NewL(const TScript* aScript, const TInt aVarDelay)
	{
	CTestDataCallCallBack* aA=new(ELeave) CTestDataCallCallBack(aScript, aVarDelay);
	CleanupStack::PushL(aA);
	aA->ConstructL();
	CleanupStack::Pop();
	return aA;
	}

CTestDataCallCallBack::CTestDataCallCallBack(const TScript* aScript) : iScript(aScript)
/**
 * This method is the constructor for CTestDataCallCallBack.
 *
 * @param aScript: pointer to the specifics of the script to run. 
 * @note Initializes private data "aScript" to received parameter.
 */
	{}

CTestDataCallCallBack::CTestDataCallCallBack(const TScript* aScript, const TInt aVarDelay) : CATScriptEng(aVarDelay), iScript(aScript)
	{}

void CTestDataCallCallBack::ConstructL()
/**
 * This method is used to implement the 2 Phase Constructor for CTestDataCallCallBack.
 * This method uses the CATBase ConstructL to configure the port to be used.
 *
 * @leave  Leaves if CATBase leaves.
 */
	{
	CATScriptEng::ConstructL();
	}

TInt CTestDataCallCallBack::Start()
/**
 * This method is defined as a pure virtual function that must be implemented.
 * This method is currently not used by the Etel regression test harness.
 * Instead of using this method to start the scripts, the CTestTxMess::Start()
 * method is used to start the scripts.  The CTestTxMess::Start() is called by
 * the responder thread of the scripting engine to start the execution of the script.
 *
 * @return KErrNone.
 */
	{
	StartScript(iScript);
	return KErrNone;
	}

void CTestDataCallCallBack::SpecificAlgorithmL(TInt /* aParam */)
/**
 * This method is defined as a pure virtual function that must be implemented.
 * This method is currently not used by the Etel regression test harness.
 * Instead of using this method to perform an algorithm specific to this test,
 * the CTestTxMess::SpecificAlgorithm() method is used.  The CTestTxMess::SpecificAlgorithm() 
 * is called by the scripting engine to perform the test specific algorithm.
 */
	{
	}

void CTestDataCallCallBack::Complete(TInt aError)
/**
 * This method is defined as a pure virtual function that must be implemented.
 * This method is currently not used by the Etel regression test harness.
 * Instead of using this method to end the scripts, the CTestTxMess::Complete()
 * method is used to end the scripts.  The CTestTxMess::Complete() is called by
 * the scripting engine to end the execution of the script.
 */
	{
	iReturnValue=aError;
	CActiveScheduler::Stop();
	}
