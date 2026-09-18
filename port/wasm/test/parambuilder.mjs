// A parameter::fbs::ParamItem flatbuffer, built in JavaScript.
//
// Only the test uses this. The editor never needs it -- Kaledi gets its blobs
// from the same place every other Kanardia tool does, out of ParamStorage --
// and this exists so that smoke.mjs can push a parameter with real coloured
// bands without a board, a Nesis or a C++ host tool in the loop. Without it
// setParameter(), which is the module's headline call, would go untested.
//
// Written against the flatbuffers runtime's low-level builder rather than
// generated code, because generating the code would mean carrying flatc. The
// field slots below are the declaration order in
// Common/FBS/ParamStorageItem.fbs, which is what a flatbuffer vtable slot is;
// if that schema ever gains a field in the middle, this has to follow.

// The flatbuffers runtime lives where lv_font_conv does, under tools/, which is
// not on Node's resolution path from here. Rooting a require() there finds it
// without the caller having to set NODE_PATH:
//
//     npm install --prefix tools flatbuffers
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';

const toolsDir = resolve(dirname(fileURLToPath(import.meta.url)), '../../../tools');
const flatbuffers = createRequire(resolve(toolsDir, 'noop.cjs'))('flatbuffers');

// table Bands { low, values, colors, scale, alm_text, alm_audio,
//               alm_delay_s, alm_cond_bits, alm_sig_bits }
const BANDS_LOW = 0, BANDS_VALUES = 1, BANDS_COLORS = 2, BANDS_SCALE = 3;

// table ParamItem { can_id, enabled, count, full_name, short_name, tiny_name,
//                   time_constant, bands, attributes }
const ITEM_CAN_ID = 0, ITEM_ENABLED = 1, ITEM_COUNT = 2,
      ITEM_FULL = 3, ITEM_SHORT = 4, ITEM_TINY = 5,
      ITEM_TC = 6, ITEM_BANDS = 7, ITEM_ATTRS = 8;

// table Attributes { attr_id, values }
const ATTR_ID = 0, ATTR_VALUES = 1;

// parameter::Color, as ParamDefines.h numbers it.
export const Color = { NoColor: 0, Red: 1, Yellow: 2, Green: 3, Blue: 4, White: 5 };

function vector(b, values, write, elemSize) {
  b.startVector(elemSize, values.length, elemSize);
  for(let i = values.length - 1; i >= 0; i--) write(values[i]);
  return b.endVector();
}

// bands: { low, stops: [{ value, color }] }
//
// A stop is the top of a band and the colour it is drawn in, which is how
// parameter::Bands::Append() takes them. `scale` is the alarm scale Common
// carries alongside; zero everywhere, as AppParameters.cpp writes it.
export function buildParamItem({ canId, count = 1, enabled = 1, timeConstant = 400,
                                 names, bands }) {
  const b = new flatbuffers.Builder(1024);

  const values = bands.stops.map(s => s.value);
  const colors = bands.stops.map(s => s.color);
  const scale  = bands.stops.map(() => 0);

  const oValues = vector(b, values, v => b.addFloat32(v), 4);
  const oColors = vector(b, colors, v => b.addInt8(v),    1);
  const oScale  = vector(b, scale,  v => b.addFloat32(v), 4);

  b.startObject(9);
  b.addFieldFloat32(BANDS_LOW, bands.low, 0);
  b.addFieldOffset(BANDS_VALUES, oValues, 0);
  b.addFieldOffset(BANDS_COLORS, oColors, 0);
  b.addFieldOffset(BANDS_SCALE,  oScale,  0);
  const oBands = b.endObject();

  // ParamStorage::ApplyTo() wants attr_id and values present and of equal
  // size, so an empty pair of vectors rather than no table at all.
  const oAttrId  = vector(b, [], () => {}, 1);
  const oAttrVal = vector(b, [], () => {}, 4);
  b.startObject(2);
  b.addFieldOffset(ATTR_ID,     oAttrId,  0);
  b.addFieldOffset(ATTR_VALUES, oAttrVal, 0);
  const oAttrs = b.endObject();

  const oFull  = b.createString(names.long);
  const oShort = b.createString(names.short);
  const oTiny  = b.createString(names.tiny);

  b.startObject(9);
  b.addFieldInt16(ITEM_CAN_ID, canId, 0);
  b.addFieldInt8(ITEM_ENABLED, enabled, 0);
  b.addFieldInt8(ITEM_COUNT, count, 0);
  b.addFieldOffset(ITEM_FULL,  oFull,  0);
  b.addFieldOffset(ITEM_SHORT, oShort, 0);
  b.addFieldOffset(ITEM_TINY,  oTiny,  0);
  b.addFieldFloat32(ITEM_TC, timeConstant, 0);
  b.addFieldOffset(ITEM_BANDS, oBands, 0);
  b.addFieldOffset(ITEM_ATTRS, oAttrs, 0);
  const oItem = b.endObject();

  // Bare root, not size-prefixed and with no file identifier -- what
  // ParamStorage::GetParameterFB() produces and what the module expects.
  b.finish(oItem);
  return b.asUint8Array();
}
