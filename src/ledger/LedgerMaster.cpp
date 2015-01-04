// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "LedgerMaster.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "lib/json/json.h"
#include "ledger/LedgerDelta.h"
#include "crypto/Hex.h"
#include "crypto/Sign.h"
#include "crypto/Base58.h"

/*
The ledger module:
    1) gets the externalized tx set
    2) applies this set to the previous ledger
    3) sends the resultMeta somewhere
    4) sends the changed entries to the CLF
    5) saves the changed entries to SQL
    6) saves the ledger hash and header to SQL
    7) sends the new ledger hash and the tx set to the history
    8) sends the new ledger hash and header to the TxHerder
    

catching up to network:
    1) Wait for FBA to tell us what the network is on now
    2) Ask network for the the delta between what it has now and our ledger last ledger  

    // TODO.1 need to make sure the CLF and the SQL Ledger are in sync on start up
    // TODO.1 make sure you validate incoming Deltas to see that it gives you the CLF you want
    // TODO.1 do we need to store some validation history?

*/
namespace stellar
{

LedgerMaster::LedgerMaster(Application& app) : mApp(app), mDatabase(app)
{
	mCaughtUp = false;
    //syncWithCLF();
}

void LedgerMaster::startNewLedger()
{
    ByteSlice bytes("masterpassphrasemasterpassphrase");
    std::string b58SeedStr = toBase58Check(VER_SEED, bytes);
    SecretKey skey = SecretKey::fromBase58Seed(b58SeedStr);
    AccountFrame masterAccount(skey.getPublicKey());
    masterAccount.mEntry.account().balance = 100000000000000;
    Json::Value result;
    masterAccount.storeAdd(result, *this);

    
    mCurrentHeader.baseFee = mApp.getConfig().DESIRED_BASE_FEE;
    mCurrentHeader.totalCoins = masterAccount.mEntry.account().balance;
    mCurrentHeader.ledgerSeq = 1;
}

int64_t LedgerMaster::getTxFee()
{
    return mCurrentHeader.baseFee; 
}

int64_t LedgerMaster::getMinBalance(int32_t ownerCount)
{
    return 0; // TODO.2
}

int64_t LedgerMaster::getLedgerNum()
{
    return mCurrentHeader.ledgerSeq;
}

LedgerHeader& LedgerMaster::getCurrentLedgerHeader()
{
    return mCurrentHeader;
}

// make sure our state is consistent with the CLF
void LedgerMaster::syncWithCLF()
{
    LedgerHeaderPtr clfHeader=mApp.getCLFGateway().getCurrentHeader();

    if(clfHeader->hash == mCurrentHeader.hash)
    {
        CLOG(DEBUG, "Ledger") << "CLF and SQL headers match.";
    } else
    {  // ledgers don't match
        // LATER try to sync them
        CLOG(ERROR, "Ledger") << "CLF and SQL headers don't match. Aborting";
    }
}

// called by txherder
void LedgerMaster::externalizeValue(const SlotBallot& slotBallot, TxSetFramePtr txSet)
{
    if(mCurrentHeader.hash == slotBallot.ballot.previousLedgerHash)
    {
        mCaughtUp = true;
        closeLedger(txSet);
    }
    else
    { // we need to catch up
        mCaughtUp = false;
        if(mApp.getState() == Application::CATCHING_UP_STATE)
        {  // we are already trying to catch up
            CLOG(DEBUG, "Ledger") << "Missed a ledger while trying to catch up.";
        } else
        {  // start trying to catchup
            startCatchUp();
        }
    }
}

// we have some last ledger that is in the DB
// we need to 
void LedgerMaster::startCatchUp()
{
    mApp.setState(Application::CATCHING_UP_STATE);
    mApp.getOverlayGateway().fetchDelta(mCurrentHeader.hash, mCurrentHeader.ledgerSeq );

}

// called by CLF
// when we either get in a delta from the network or the CLF computes the next ledger hash
// delta is null in the second case
void LedgerMaster::recvDelta(CLFDeltaPtr delta, LedgerHeaderPtr header)
{
    if(delta)
    { // we got this delta in from the network
        mApp.setState(Application::CONNECTED_STATE);
            
    } else
    {

    }
}

void LedgerMaster::closeLedger(TxSetFramePtr txSet)
{
    LedgerDelta ledgerDelta;
    mDatabase.beginTransaction();
    for(auto tx : txSet->mTransactions)
    {
        try {
            TxDelta delta;
            tx->apply(delta,mApp);

            Json::Value txResult;
            txResult["id"] = binToHex(tx->getHash());
            txResult["code"] = tx->getResultCode();
            txResult["ledger"] = (Json::UInt64)mCurrentHeader.ledgerSeq;

            delta.commitDelta(txResult, ledgerDelta, *this );

            
        }catch(...)
        {
            CLOG(ERROR, "Ledger") << "Exception during tx->apply";
        }
    }
    mDatabase.endTransaction(false);
    
    // TODO.2 give the LedgerDelta to the Bucketlist
}


}
