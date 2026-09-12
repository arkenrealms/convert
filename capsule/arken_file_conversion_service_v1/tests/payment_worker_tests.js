import cases from './payment-packets.json';
import {advancePayment} from './runtime.js';
export const payments={async test(){
  const packets=JSON.parse(cases);
  if(packets.length<100)throw Error('native payment corpus incomplete');
  for(let i=0;i<packets.length;i++){
    const actual=advancePayment(packets[i].input),expected=JSON.parse(packets[i].expected);
    if(JSON.stringify(actual)!==JSON.stringify(expected))throw Error(`payment packet ${i} disagrees with native C++`);
  }
  console.log('Native/Wasm payment packets matched',packets.length);
}};
