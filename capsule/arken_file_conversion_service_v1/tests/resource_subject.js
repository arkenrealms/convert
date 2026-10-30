import current from './production_worker.js';
export {ConversionPaymentLedger} from './production_worker.js';
export {createProductionWorker as createResourceWorker} from './production_worker.js';
export const createDefaultResourceWorker=()=>current;
export const resourceGuardEvidence=()=>({guarded:false});
