import {createWorker} from './worker.js';
import {createPaidConversion} from './paid_conversion.js';
import {planProductionRequest} from './runtime.js';
import {createProductionStartup} from './production_startup.js';
export {ConversionPaymentLedger} from './payment_ledger.js';
export function createProductionWorker({fetcher,startup=createProductionStartup({fetcher}),clock,send,readChain}={}) {
  return createWorker(createPaidConversion({profileId:'base-mainnet-v1',requestPlan:planProductionRequest,startup,clock,send,fetcher,readChain,
    executionContext:env=>({cdpCredential:{keyId:env.CDP_API_KEY_ID,keySecret:env.CDP_API_KEY_SECRET},rpcClientKey:env.CDP_RPC_CLIENT_KEY??null})}));
}
export default createProductionWorker();
