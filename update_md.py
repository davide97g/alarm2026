import sys

with open("GEMINI.md", "r") as f:
    lines = f.readlines()

out_lines = []
resolved_bugs = []
in_resolved_section = False

for line in lines:
    if line.startswith("### ~~8."):
        in_resolved_section = True
    
    if in_resolved_section:
        if line.startswith("### 9."):
            in_resolved_section = False
            out_lines.append(line)
        else:
            resolved_bugs.append(line)
    else:
        out_lines.append(line)

insert_idx = 0
for i, line in enumerate(out_lines):
    if line.startswith("## Problemi Tangibili Rimanenti"):
        insert_idx = i
        break

if insert_idx > 0 and len(resolved_bugs) > 0:
    final_lines = out_lines[:insert_idx] + resolved_bugs + out_lines[insert_idx:]
    with open("GEMINI.md", "w") as f:
        f.writelines(final_lines)
    print("Aggiornamento riuscito.")
else:
    print("Errore: sezioni non trovate.")
