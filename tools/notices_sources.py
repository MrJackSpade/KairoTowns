"""KairoTowns's third-party notices: the sources shared/tools/notices.py writes
kairotowns/src/main/assets/THIRD_PARTY_NOTICES.txt from. Run it again after an import."""
from notices import text

OUT = 'kairotowns/src/main/assets/THIRD_PARTY_NOTICES.txt'

HEADER = '''KairoTowns third-party notices
These notices apply to the emulator code, libraries, and C++ runtime in this package.'''

# (title, source, [text]) in the order shown. Every Tsugaru source file is Soji Yamakawa's.
SECTIONS = [
    ('Tsugaru v20260522 by Soji Yamakawa (CaptainYS)', 'third_party/tsugaru/LICENSE', [text('tsugaru/LICENSE')]),
]
