/*
 * Crossword pkjs: voice normalization for the watch.
 *
 * The watch owns the game; the phone only turns a dictation transcript into
 * letters for the current entry, using the entry's known answer to do it well.
 * The transcript is read BOTH as a spoken word and as spelled-out letters ("bee
 * oh why", NATO "bravo", "b as in boy", "b. o. y."), and the closer reading wins.
 *
 * MODE is the watch's checking setting, because snapping a near miss to the
 * known answer is itself a correctness check:
 *   MODE 0 (check every word): a near miss by sound or spelling snaps to the
 *     answer (conf 1); anything else fills the player's guess (conf 0).
 *   MODE 1 (check at the end):  only an exact homophone is corrected, so the
 *     player learns nothing from the fill; anything else is filled literally.
 *
 * Reply: { RESP_LETTERS, RESP_STATUS (1 = nothing usable), RESP_CONF, SEQ }
 *
 * Puzzles are bundled on the watch; any stale keys an earlier version left in
 * localStorage are cleared once on launch.
 */

// ---------------------------------------------------------------------------
// Metaphone (compact). Good enough to catch English homophones like
// KNIGHT/NIGHT, SEA/SEE/C-less cases, SAIL/SALE, FLOWER/FLOUR, RIGHT/WRITE.
// ---------------------------------------------------------------------------
function metaphone(word) {
  var w = String(word).toUpperCase().replace(/[^A-Z]/g, '');
  if (!w) return '';
  var isVowel = function (c) { return 'AEIOU'.indexOf(c) >= 0; };

  // Transform troublesome leading pairs.
  if (/^(AE|GN|KN|PN|WR)/.test(w)) w = w.substring(1);
  else if (w[0] === 'X') w = 'S' + w.substring(1);
  else if (/^WH/.test(w)) w = 'W' + w.substring(2);

  var out = '';
  var len = w.length;
  var i = 0;
  // First letter: keep a leading vowel.
  if (isVowel(w[0])) { out += w[0]; i = 1; }

  function at(n) { return (n >= 0 && n < len) ? w[n] : ''; }

  for (; i < len; i++) {
    var c = w[i];
    var prev = at(i - 1);
    var next = at(i + 1);
    var next2 = at(i + 2);
    if (c === prev && c !== 'C') continue; // skip doubles

    switch (c) {
      case 'A': case 'E': case 'I': case 'O': case 'U':
        // vowels only kept at the start (handled above)
        break;
      case 'B':
        if (!(i === len - 1 && prev === 'M')) out += 'B';
        break;
      case 'C':
        if (next === 'I' && next2 === 'A') out += 'X';
        else if (next === 'H') { out += (prev === 'S') ? 'K' : 'X'; i++; }
        else if (/[IEY]/.test(next)) { if (prev !== 'S') out += 'S'; }
        else out += 'K';
        break;
      case 'D':
        if (next === 'G' && /[IEY]/.test(next2)) { out += 'J'; i += 2; }
        else out += 'T';
        break;
      case 'G':
        if (next === 'H') {
          if (!(i > 0 && !isVowel(at(i - 1)))) { /* GH often silent */ }
          if (isVowel(next2)) out += 'K';
          i++;
        } else if (next === 'N') { /* silent G in GN */ }
        else if (/[IEY]/.test(next)) out += 'J';
        else out += 'K';
        break;
      case 'H':
        if (isVowel(prev) && !isVowel(next)) { /* silent */ }
        else if (/[CSPTG]/.test(prev)) { /* handled by prev */ }
        else out += 'H';
        break;
      case 'J': out += 'J'; break;
      case 'K': if (prev !== 'C') out += 'K'; break;
      case 'L': out += 'L'; break;
      case 'M': out += 'M'; break;
      case 'N': out += 'N'; break;
      case 'P': if (next === 'H') { out += 'F'; i++; } else out += 'P'; break;
      case 'Q': out += 'K'; break;
      case 'R': out += 'R'; break;
      case 'S':
        if (next === 'H') { out += 'X'; i++; }
        else if (next === 'I' && /[OA]/.test(next2)) out += 'X';
        else out += 'S';
        break;
      case 'T':
        if (next === 'H') { out += '0'; i++; }
        else if (next === 'I' && /[OA]/.test(next2)) out += 'X';
        else out += 'T';
        break;
      case 'V': out += 'F'; break;
      // Keep a glide W/Y only before a vowel AND not after a vowel, so the
      // vowel-glide in FLOWER (-> FLOUR) drops but WATER/YES keep their onset.
      case 'W': case 'Y': if (isVowel(next) && !isVowel(prev)) out += c; break;
      case 'X': out += 'KS'; break;
      case 'Z': out += 'S'; break;
      default: break;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Letter-name homophones and NATO words for Spell mode.
// ---------------------------------------------------------------------------
var LETTER_WORD = {
  ay: 'A', aye: 'A', eh: 'A',
  bee: 'B', be: 'B',
  cee: 'C', see: 'C', sea: 'C',
  dee: 'D',
  ee: 'E',
  ef: 'F', eff: 'F',
  gee: 'G',
  aitch: 'H', haitch: 'H',
  eye: 'I', ai: 'I',
  jay: 'J',
  kay: 'K', kaye: 'K',
  el: 'L', ell: 'L',
  em: 'M',
  en: 'N',
  oh: 'O', owe: 'O',
  pee: 'P', pea: 'P',
  cue: 'Q', queue: 'Q', kew: 'Q',
  ar: 'R', are: 'R',
  ess: 'S', es: 'S',
  tee: 'T', tea: 'T',
  you: 'U', yew: 'U', ewe: 'U',
  vee: 'V',
  doubleu: 'W', 'double-u': 'W',
  ex: 'X', ecks: 'X',
  why: 'Y', wye: 'Y',
  zee: 'Z', zed: 'Z'
};
var NATO = {
  alpha: 'A', alfa: 'A', bravo: 'B', charlie: 'C', delta: 'D', echo: 'E',
  foxtrot: 'F', golf: 'G', hotel: 'H', india: 'I', juliet: 'J', juliett: 'J',
  kilo: 'K', lima: 'L', mike: 'M', november: 'N', oscar: 'O', papa: 'P',
  quebec: 'Q', romeo: 'R', sierra: 'S', tango: 'T', uniform: 'U', victor: 'V',
  whiskey: 'W', xray: 'X', 'x-ray': 'X', yankee: 'Y', zulu: 'Z'
};

// Parse spoken letters into a string. Handles "b as in boy", NATO, letter
// names, and bare letters ("b. o. y." / "B O Y").
function parseSpelled(transcript) {
  var t = String(transcript).toLowerCase();
  // "b as in boy" / "b for boy" / "b like boy" -> keep the first letter only.
  t = t.replace(/\b([a-z])\s+(?:as|for|like)\s+in?\s+[a-z]+/g, ' $1 ');
  t = t.replace(/\b([a-z])\s+(?:as|for|like)\s+[a-z]+/g, ' $1 ');
  // "double u" -> token
  t = t.replace(/\bdouble\s*u\b/g, ' doubleu ');
  var tokens = t.split(/[^a-z-]+/).filter(function (x) { return x; });
  var out = '';
  for (var i = 0; i < tokens.length; i++) {
    var tok = tokens[i];
    if (tok.length === 1 && tok >= 'a' && tok <= 'z') { out += tok.toUpperCase(); continue; }
    if (NATO[tok]) { out += NATO[tok]; continue; }
    if (LETTER_WORD[tok]) { out += LETTER_WORD[tok]; continue; }
    // A run of bare letters with no separators, e.g. "boy" is NOT letters:
    // ignore unknown multi-char tokens rather than guessing.
  }
  return out;
}

function lettersOnly(s) { return String(s).toUpperCase().replace(/[^A-Z]/g, ''); }

// Levenshtein edit distance (small strings, so the simple DP is fine).
function lev(a, b) {
  var m = a.length, n = b.length, d = [], i, j;
  if (!m) return n; if (!n) return m;
  for (i = 0; i <= m; i++) d[i] = [i];
  for (j = 0; j <= n; j++) d[0][j] = j;
  for (i = 1; i <= m; i++) for (j = 1; j <= n; j++)
    d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
  return d[m][n];
}
// How far a reading may sit from the answer and still count as "you said it".
// Two budgets: raw letters (recognizer slips) and phonetic code (homophones and
// near-homophones). Both are tight, because every snap reveals the answer: with
// the v0.4 budgets a random corpus answer said for another snapped 27.6% of the
// time ("stood up" -> UPENDED); with these it is about 0.3%.
function thLetters(len) { return len <= 4 ? 1 : len <= 7 ? 2 : 3; }
function thMeta(len) { return len <= 5 ? 0 : 1; }

var MODE_EVERY_WORD = 0;
var MODE_AT_END = 1;

// ---------------------------------------------------------------------------
// Main normalization.
//
// Dictation collapses spelled letters into words ("see a tee" -> "seat") and
// mis-hears isolated crosswordese, so we build BOTH readings of the transcript
// (the spoken word(s) and the spelled-out letters) and compare each against the
// ONE known answer. A reading must be about the answer's length to snap at all:
// a short fragment of the transcript can never be "the answer said slightly
// wrong". Only ever compared against the one answer, so this cannot leak a
// different word.
//   conf 1 = we believe you said the answer (snapped / exact);
//   conf 0 = we could not, so we filled what you said to help the crossings.
// ---------------------------------------------------------------------------
function readings(transcript) {
  var words = String(transcript).toLowerCase().split(/[^a-z]+/).filter(function (x) { return x; });
  var cands = [lettersOnly(transcript)];
  for (var i = 0; i < words.length; i++) {
    cands.push(words[i].toUpperCase());
    if (i + 1 < words.length) cands.push((words[i] + words[i + 1]).toUpperCase());
  }
  var spelled = parseSpelled(transcript);
  if (spelled) cands.push(spelled);
  return { words: words, cands: cands, spelled: spelled };
}

// Fill what the player said, with no reference to the answer beyond its length.
function literalFill(r, transcript, L) {
  for (var w = 0; w < r.words.length; w++) if (r.words[w].length === L)
    return { letters: r.words[w].toUpperCase(), status: 0, conf: 0 };
  // The spelled reading wins only when the transcript really was letters: it
  // covers the answer, or every spoken token parsed as a letter.
  if (r.spelled && (r.spelled.length >= L || r.spelled.length >= r.words.length))
    return { letters: r.spelled.substring(0, L), status: 0, conf: 0 };
  var joined = lettersOnly(transcript);
  if (joined) return { letters: joined.substring(0, L), status: 0, conf: 0 };
  return { letters: '', status: 1, conf: 0 };
}

function normalize(mode, transcript, answer, len) {
  answer = lettersOnly(answer);
  var L = answer.length;
  if (!L) return { letters: '', status: 1, conf: 0 };
  var r = readings(transcript);
  var c, cand;

  // Exact hit on any reading -> the answer (in either mode: it is what they said).
  for (c = 0; c < r.cands.length; c++) if (r.cands[c] === answer)
    return { letters: answer, status: 0, conf: 1 };

  var am = metaphone(answer);
  if (mode === MODE_AT_END) {
    // Homophone only: identical phonetic code and about the same length. Fixes
    // the spelling the recognizer chose ("night" for KNIGHT) without confirming
    // anything else.
    for (c = 0; c < r.cands.length; c++) {
      cand = r.cands[c];
      if (cand && Math.abs(cand.length - L) <= 1 && am && metaphone(cand) === am)
        return { letters: answer, status: 0, conf: 1 };
    }
    return literalFill(r, transcript, L);
  }

  // Check-every-word: near miss by spelling OR sound, on a reading about the
  // answer's length.
  for (c = 0; c < r.cands.length; c++) {
    cand = r.cands[c];
    if (!cand || Math.abs(cand.length - L) > 1) continue;
    if (lev(cand, answer) <= thLetters(L)) return { letters: answer, status: 0, conf: 1 };
    if (am && lev(metaphone(cand), am) <= thMeta(L)) return { letters: answer, status: 0, conf: 1 };
  }
  return literalFill(r, transcript, L);
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------
if (typeof Pebble !== 'undefined') {
  Pebble.addEventListener('ready', function () {
    // The retired library client kept indexes, puzzle bodies, a seen set and
    // an LRU under xw:*; nothing on the phone is needed now, so clear it all.
    try {
      var stale = [];
      for (var i = 0; i < localStorage.length; i++) {
        var k = localStorage.key(i);
        if (k && k.indexOf('xw:') === 0) stale.push(k);
      }
      for (var j = 0; j < stale.length; j++) localStorage.removeItem(stale[j]);
    } catch (e) { /* ignore */ }
  });
  Pebble.addEventListener('appmessage', function (e) {
    var p = e.payload || {};
    if (p.CMD === 1) {
      var res = normalize(p.MODE || 0, p.TRANSCRIPT || '', p.ANSWER || '', p.LEN || 0);
      // SEQ is echoed so the watch can drop a reply for an entry it has left.
      Pebble.sendAppMessage({ RESP_LETTERS: res.letters, RESP_STATUS: res.status,
                              RESP_CONF: res.conf, SEQ: p.SEQ || 0 });
    }
  });
}

// Exported for tests.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { metaphone: metaphone, parseSpelled: parseSpelled, normalize: normalize,
                     lev: lev, MODE_EVERY_WORD: MODE_EVERY_WORD, MODE_AT_END: MODE_AT_END };
}
