"use strict";

var vfs;
try{
  vfs = require( "./build/Debug/sack_vfs.node" );
} catch(err) { try { /*console.log( err ); */vfs = require( "./build/Release/sack_vfs.node" ); } catch( err ){  console.log( err )} }

let tmpParse6 = vfs.JSON6.parse;
let tmpParse = vfs.JSON.parse;

function JSONReviverWrapper (parser) {
   return (string,reviver)=> {
	var result = parser( string );
        return typeof reviver === 'function' ? (function walk(holder, key) {
            var k, v, value = holder[key];
            if (value && typeof value === 'object') {
                for (k in value) {
                    if (Object.prototype.hasOwnProperty.call(value, k)) {
                        v = walk(value, k);
                        if (v !== undefined) {
                            value[k] = v;
                        } else {
                            delete value[k];
                        }
                    }
                }
            }
            return reviver.call(holder, key, value);
        }({'': result}, '')) : result;
   }
}

vfs.JSON6.parse = JSONReviverWrapper( tmpParse6 );

vfs.JSON.parse = JSONReviverWrapper( tmpParse );


process.on( 'beforeExit', ()=>vfs.Thread() );
const thread = vfs.Thread(process._tickDomainCallback);
module.exports =exports= vfs;

