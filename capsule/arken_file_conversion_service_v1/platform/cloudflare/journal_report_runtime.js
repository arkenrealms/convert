import module from './conversion-journal-report.wasm';
import {createPaidJournalReporter} from './report_runtime.js';
export function createConversionJournalReporter(){
 return createPaidJournalReporter(module,{exportPrefix:'arken_convert_report'});
}
