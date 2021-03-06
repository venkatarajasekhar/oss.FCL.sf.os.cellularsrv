// Copyright (c) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
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
// The TEFUnit header file which tests the CallCost
// functional unit of the Common TSY.
// 
//

#ifndef CCTSYCALLCOSTFU_H
#define CCTSYCALLCOSTFU_H

#include <test/tefunit.h>

#include <etelmm.h>
#include <etelmmcs.h>

#include "cctsycomponenttestbase.h"

class CCTsyCallCostFU : public CCtsyComponentTestBase
	{
public:
	// Create a suite of all the tests
	static CTestSuite* CreateSuiteL(const TDesC& aName);

public:
	// Individual test steps

	void TestGetCostCaps0001L();
	void TestGetCostInfo0001L();
	void TestClearCostMeter0001L();
	void TestSetMaxCostMeter0001L();
	void TestNotifyCostCapsChange0001L();
	void TestNotifyCostInfoChange0001L();
	void TestSetPuct0001L();


private:


	}; // class CCTsyCallCostFU

#endif // CCTSYCALLCOSTFU_H

