#!/usr/bin/env python3
"""Reference implementation of the chat stress corpus generator.

Mirrors src/corpus.cpp line for line (hash, users, Zipf thresholds,
timestamps, text). It exists to produce the golden values that
tests/corpus_tests.cpp checks, without a compiler:

    python3 -B tools/corpus_reference.py            # prints the golden table
    python3 -B tools/corpus_reference.py --cpp      # prints it as C++ rows

Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE).
"""

import argparse
import math
import sys

MASK = (1 << 64) - 1
FIELD_MULTIPLIER = 0xD1B54A32D192ED03
USER_SEED_TAG = 0x7573657273000000
DEFAULT_SEED = 0x6368617473747265
EPOCH_MS = 1577836800000
BLOCK = 4096
AUTHOR_RUN_LIMIT = 64
AUTHOR_SCALE = 1 << 24

F_AUTHOR_FLAG = 1
F_AUTHOR_PICK = 2
F_WEIGHT = 3
F_LENGTH = 4
F_SCRIPT = 5
F_URL = 6
F_BLOCK_SPAN = 7
F_USER_HUE = 9
F_USER_PATTERN = 10
F_TEXT = 64
F_NAME = 128

LATIN = ["ka", "lo", "mi", "ren", "tas", "vel", "dor", "shi", "an", "eth", "ul", "bra", "cen", "dai", "fo", "gri",
         "hal", "is", "jun", "kor", "lin", "mo", "nel", "or", "pa", "qui", "ros", "sa", "tem", "u", "vir", "zo"]
FOREIGN = [
    ["κα", "λο", "μη", "ρε", "τα", "νι", "σο", "φυ"],
    ["ра", "но", "ви", "ка", "ло", "ми", "ту", "же"],
    ["山", "水", "火", "木", "日", "月", "風", "花"],
    ["سل", "ام", "كت", "اب", "مر", "حب", "نو", "ري"],
]
URL_PREFIX = "https://example.test/"
URL_ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"


def mix64(value):
    z = (value + 0x9E3779B97F4A7C15) & MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return z ^ (z >> 31)


def field_hash(seed, index, field):
    inner = mix64(index ^ ((field * FIELD_MULTIPLIER) & MASK))
    return mix64(seed ^ inner)


class Stream:
    def __init__(self, seed, index, base):
        self.seed, self.index, self.base, self.drawn = seed, index, base, 0

    def next(self):
        value = field_hash(self.seed, self.index, self.base + self.drawn)
        self.drawn += 1
        return value


class Corpus:
    def __init__(self, seed=DEFAULT_SEED, users=5000, mixed=False):
        self.seed, self.users, self.mixed = seed, users, mixed
        self.thresholds = self._thresholds()
        self.block_start = [EPOCH_MS]

    def _thresholds(self):
        cumulative, total = [], 0.0
        for rank in range(self.users):
            total = total + 1.0 / math.pow(rank + 1.0, 1.1)
            cumulative.append(total)
        out = [int(math.floor(c / total * AUTHOR_SCALE + 0.5)) for c in cumulative]
        out[-1] = AUTHOR_SCALE
        return out

    def h(self, index, field):
        return field_hash(self.seed, index, field)

    def user(self, user):
        seed = self.seed ^ USER_SEED_TAG
        stream = Stream(seed, user, F_NAME)
        words = []
        for _ in range(2):
            word = ""
            for syllable in range(2):
                piece = LATIN[stream.next() % 32]
                word += piece.capitalize() if syllable == 0 else piece
            words.append(word)
        hue = field_hash(seed, user, F_USER_HUE) % 360
        pattern = field_hash(seed, user, F_USER_PATTERN) & 0x7FFF
        return " ".join(words), hue, pattern

    def length(self, index):
        return 100 + self.h(index, F_LENGTH) % 901

    def author(self, index):
        draw = index
        while draw % AUTHOR_RUN_LIMIT != 0 and self.h(draw, F_AUTHOR_FLAG) % 100 < 60:
            draw -= 1
        pick = self.h(draw, F_AUTHOR_PICK) >> 40
        rank = 0
        while rank < len(self.thresholds) and self.thresholds[rank] <= pick:
            rank += 1
        return min(rank, len(self.thresholds) - 1) + 1

    def weight(self, index):
        value = self.h(index, F_WEIGHT)
        choice, spread = value % 1000, value >> 10
        return 1 + spread % 20 if choice < 900 else 200 + spread % 801

    def span(self, block):
        return BLOCK * (20000 + self.h(block, F_BLOCK_SPAN) % 100001)

    def timestamp(self, index):
        block = index // BLOCK
        while len(self.block_start) <= block:
            b = len(self.block_start) - 1
            self.block_start.append(self.block_start[-1] + self.span(b))
        first = block * BLOCK
        prefix = sum(self.weight(i) for i in range(first, index))
        total = sum(self.weight(i) for i in range(first, first + BLOCK))
        return self.block_start[block] + self.span(block) * prefix // total

    def text(self, index):
        target = self.length(index)
        mixed = self.mixed and self.h(index, F_SCRIPT) % 100 < 15
        has_url = self.h(index, F_URL) % 100 < 2
        s = Stream(self.seed, index, F_TEXT)
        url_word = s.next() % 12 if has_url else -1
        sentence_words = 4 + s.next() % 11
        in_sentence = word_number = 0
        capital = True
        out = bytearray()
        while len(out) < target:
            if out:
                out += b" "
            if word_number == url_word:
                out += URL_PREFIX.encode()
                for _ in range(40 + s.next() % 51):
                    out += URL_ALPHABET[s.next() % 36].encode()
            elif mixed and s.next() % 100 < 25:
                script = s.next() % 4
                for _ in range(1 + s.next() % 3):
                    out += FOREIGN[script][s.next() % 8].encode()
            else:
                shape = s.next()
                syllables = 1 + shape % 3 + (1 if (shape >> 8) % 8 == 0 else 0)
                for syllable in range(syllables):
                    piece = LATIN[s.next() % 32]
                    out += (piece.capitalize() if syllable == 0 and capital else piece).encode()
            capital = False
            in_sentence += 1
            word_number += 1
            if in_sentence >= sentence_words:
                mark = s.next() % 10
                out += b"?" if 7 <= mark < 9 else (b"!" if mark >= 9 else b".")
                capital = True
                in_sentence = 0
                sentence_words = 4 + s.next() % 11
            elif s.next() % 10 == 0:
                out += b","
        if len(out) > target:
            cut = target
            while cut > 0 and (out[cut] & 0xC0) == 0x80:
                cut -= 1
            del out[cut:]
        while out and out[-1:] == b" ":
            del out[-1:]
        return bytes(out)


GOLDEN_INDICES = [0, 1, 2, 63, 64, 4095, 4096, 123456, 9999999]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp", action="store_true")
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=DEFAULT_SEED)
    args = parser.parse_args()
    corpus = Corpus(args.seed)
    for index in GOLDEN_INDICES:
        text = corpus.text(index)
        head = text[:32].decode("ascii", "replace")
        row = (index, corpus.author(index), corpus.timestamp(index), corpus.length(index), len(text), head)
        if args.cpp:
            print('    {%dU, %dU, %d, %dU, %dU, "%s"},' % row)
        else:
            print("%d author=%d ts=%d length=%d bytes=%d head=%r" % row)
    for user in (1, 2, 5000):
        name, hue, pattern = corpus.user(user)
        if args.cpp:
            print('    {%dU, "%s", %dU, %dU},' % (user, name, hue, pattern))
        else:
            print("user %d %r hue=%d pattern=%d" % (user, name, hue, pattern))
    return 0


if __name__ == "__main__":
    sys.exit(main())
