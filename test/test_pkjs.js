// Node unit tests for the pkjs voice normalizer. Run: node test/test_pkjs.js
var m = require('../src/pkjs/index.js');
var EVERY = m.MODE_EVERY_WORD, END = m.MODE_AT_END;

var pass = 0, fail = 0;
function eq(got, want, label) {
  if (got === want) { pass++; }
  else { fail++; console.log('FAIL ' + label + ': got ' + JSON.stringify(got) + ' want ' + JSON.stringify(want)); }
}
function letters(mode, t, a) { return m.normalize(mode, t, a, a.length).letters; }
function conf(mode, t, a) { return m.normalize(mode, t, a, a.length).conf; }

// --- Spelled readings (either mode; spelling is literal) ---------------------
eq(letters(EVERY, 'bee oh why', 'BOY'), 'BOY', 'spell letter-names');
eq(letters(EVERY, 'B. O. Y.', 'BOY'), 'BOY', 'spell bare letters w/ dots');
eq(letters(EVERY, 'bravo oscar yankee', 'BOY'), 'BOY', 'spell NATO');
eq(letters(EVERY, 'e a g l e', 'EAGLE'), 'EAGLE', 'spell single letters');
eq(letters(EVERY, 'b as in boy o y', 'BOY'), 'BOY', 'spell "b as in boy"');
eq(letters(EVERY, 'sea oh why', 'COY'), 'COY', 'spell homophone letter (sea=C)');
eq(letters(EVERY, 'double u', 'W'), 'W', 'spell double-u');
eq(letters(EVERY, 'ess a tee ee dee', 'SATED'), 'SATED', 'spell SATED');
eq(letters(END, 'bee oh why', 'BOY'), 'BOY', 'spell works in at-end mode');
eq(letters(END, 'see a tee', 'CAT'), 'CAT', 'spelled see a tee->CAT in at-end mode');
eq(letters(EVERY, 'e r r', 'ERR'), 'ERR', 'spelled e r r->ERR');

// --- Exact + homophone snap (both modes) -------------------------------------
eq(letters(EVERY, 'eagle', 'EAGLE'), 'EAGLE', 'speak exact');
eq(letters(EVERY, 'the eagle', 'EAGLE'), 'EAGLE', 'speak with article');
eq(letters(EVERY, 'night', 'KNIGHT'), 'KNIGHT', 'homophone knight/night');
eq(letters(EVERY, 'sail', 'SALE'), 'SALE', 'homophone sail/sale');
eq(letters(EVERY, 'flower', 'FLOUR'), 'FLOUR', 'homophone flower/flour');
eq(letters(EVERY, 'right', 'WRITE'), 'WRITE', 'homophone right/write');
eq(letters(END, 'night', 'KNIGHT'), 'KNIGHT', 'at-end: exact homophone still corrected');
eq(letters(END, 'sail', 'SALE'), 'SALE', 'at-end: sail/sale');
eq(letters(END, 'made', 'MEADE'), 'MEADE', 'at-end: made/MEADE is a true homophone, corrected');
eq(letters(END, 'sun', 'STUN'), 'SUN', 'at-end: sun/STUN differ in sound, filled literally');

// --- Near-miss snap only in check-every-word mode ----------------------------
eq(letters(EVERY, 'ned', 'NEB'), 'NEB', 'near-miss ned->NEB');
eq(letters(EVERY, 'made', 'MEADE'), 'MEADE', 'near-miss made->MEADE');
eq(letters(EVERY, 'eagel', 'EAGLE'), 'EAGLE', 'near-miss transposition');
eq(letters(END, 'ned', 'NEB'), 'NED', 'at-end: near miss filled literally');
eq(letters(END, 'eagel', 'EAGLE'), 'EAGEL', 'at-end: transposition filled literally');

// --- Spoilers that must NOT snap (v0.4 regressions) ---------------------------
eq(letters(EVERY, 'stood up', 'UPENDED'), 'STOODUP', 'stood up must not snap to UPENDED');
eq(letters(EVERY, 'ten', 'TENANT'), 'TEN', 'ten must not snap to TENANT');
eq(letters(EVERY, 'a', 'ABE'), 'A', 'a must not snap to ABE');
eq(letters(EVERY, 'store', 'ASTERN'), 'STORE', 'store must not snap to ASTERN');
eq(letters(EVERY, 'oh no', 'ORNATE'), 'OHNO', 'oh no must not snap to ORNATE');
eq(letters(EVERY, 'knob', 'NABOB'), 'KNOB', 'knob must not snap to NABOB');
eq(letters(EVERY, 'tiger', 'EAGLE'), 'TIGER', 'same-length wrong guess fills literally');
eq(m.normalize(EVERY, 'cat', 'EAGLE', 5).status, 0, 'wrong-length still returns letters');

// --- Confidence flag ---------------------------------------------------------
eq(conf(EVERY, 'eagle', 'EAGLE'), 1, 'conf 1 when exact');
eq(conf(EVERY, 'ned', 'NEB'), 1, 'conf 1 when snapped');
eq(conf(EVERY, 'tiger', 'EAGLE'), 0, 'conf 0 when filling a guess');
eq(conf(EVERY, 'bee oh why', 'BOY'), 1, 'conf 1 when spelled == answer');
eq(conf(EVERY, 'zzzz', 'EAGLE'), 0, 'conf 0 on junk');
eq(m.normalize(EVERY, '', 'EAGLE', 5).status, 1, 'status 1 on empty transcript');

// --- Metaphone sanity --------------------------------------------------------
eq(m.metaphone('KNIGHT'), m.metaphone('NIGHT'), 'metaphone knight==night');
eq(m.metaphone('SAIL'), m.metaphone('SALE'), 'metaphone sail==sale');
eq(m.metaphone('WRITE'), m.metaphone('RIGHT'), 'metaphone write==right');

// --- Spoiler-rate ceiling: saying one puzzle answer for another --------------
// A deterministic sample of answer pairs drawn from the bundled puzzles. Every
// snap here would hand the player an answer they did not know.
(function () {
  var fs = require('fs'), path = require('path');
  var dir = path.join(__dirname, '..', 'puzzles');
  if (!fs.existsSync(dir)) return;
  var ans = [];
  fs.readdirSync(dir).forEach(function (f) {
    fs.readFileSync(path.join(dir, f), 'utf8').split('\n').forEach(function (l) {
      var mm = /^[AD]\d+\.\s+.*?\s+~\s+([A-Za-z]+)\s*$/.exec(l);
      if (mm) ans.push(mm[1].toUpperCase());
    });
  });
  var uniq = Array.from(new Set(ans)).sort();
  if (uniq.length < 100) return;
  var seed = 12345, n = 0, hits = 0;
  function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
  for (var i = 0; i < 4000; i++) {
    var a = uniq[Math.floor(rnd() * uniq.length)], b = uniq[Math.floor(rnd() * uniq.length)];
    if (a === b) continue;
    n++;
    if (m.normalize(EVERY, a.toLowerCase(), b, b.length).conf === 1) hits++;
  }
  var rate = hits / n;
  eq(rate <= 0.01, true, 'spoiler rate <= 1% (got ' + (100 * rate).toFixed(2) + '% of ' + n + ')');
})();

console.log('\n' + pass + ' passed, ' + fail + ' failed');
if (fail) process.exit(1);
