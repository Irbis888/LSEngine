# Coding style

Follow these rules in all new and modified code:

- Put spaces around binary and assignment operators. Do not join operands to operators.
- Put each statement on its own line.
- Declare each field and variable separately on its own line, including fields in structs, classes and shader constant buffers.
- Put spaces around semantic separators, for example `float4 position : SV_POSITION` and `: SV_Target`.
- Put a space after commas and semicolons when more text follows on the same line (including `for` headers).
- Use meaningful variable and function names that describe their purpose. Avoid cryptic abbreviations and single-letter temporary names.
- Use braces and readable indentation for control flow; do not compress functions or conditional bodies onto one line.
- Preserve behavior and constant-buffer layout when applying style changes to shaders.
