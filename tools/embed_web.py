"""PlatformIO pre-build: embed the local configuration page in flash."""
from pathlib import Path
Import('env')
root = Path(env.subst('$PROJECT_DIR'))
out = Path(env.subst('$BUILD_DIR')) / 'generated'
out.mkdir(parents=True, exist_ok=True)
html = (root / 'web/config.html').read_text()
header = '#pragma once\n#include <Arduino.h>\nstatic const char configPage[] PROGMEM = R"AURORA(' + html + ')AURORA";\n'
path = out / 'web_page.h'
if not path.exists() or path.read_text() != header:
    path.write_text(header)
env.Append(CPPPATH=[str(out)])
