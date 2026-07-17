# Contributing to TethysRT

## Overview

Contrubutions are very welcome. TethysRT is a small project, but there's still lots of work to be done (especially this early on). This page attempts to capture the design philosophy and general goals of the project. First and foremost, please share your thoughts and ideas. Even if they're not completely aligned with the goals below, it can still be helpful and productive to make them known.

## Design Philosophy

TethysRT is deliberately:

1. <u>a <i>General Purpose</i> Loader for ARM Embedded Systems.</u><br>
Hitching TethysRT to a specific processor, vendor SDK, RTOS, etc. would limit its impact and adaptability.

2. <u>small</u><br>
Host-side memory efficiency is a core priority. RAM and flash are precious, and keeping TethysRT's memory footprint as small as possible (within reason) is in keeping with its overall purpose. As of this writing, the project contains a single source file under 1K lines. It probably won't stay that way forever, but it's something to keep in mind.

3. <u>agnostic</u><br>
Let’s keep TethysRT as agnostic as possible (within reason) about I/O implementations, storage backends, hosted environment details, memory ownership, etc. This is essentially a minimal ELF parser + memory relocator. Anything beyond this should be carefully justified.

## Submitting PRs

Nothing special required really. If you have an idea, implement it and submit it. Small, well-motivated changes are preferred over huge rewrites, but neither is off-limits. Make an issue first if you think it's warranted. I have yet to document the coding standard, but that's on me.

## TODOs

A few things are currently missing from the project:

- <u>Testing</u>. There's currently no unit test framework in place to test ELF parsing and symbol resolution. Ideally we should be able to exercise all relevant sad paths (malformed ELF headers, missing symbols, invalid relocations, etc.) without requiring hardware.

- <u>Optimizations.</u> TethysRT is not yet fully optimized for size or speed. Hunting out extra bytes and cycles is an ongoing priority.

- <u>Documentation.</u> This is another ongoing todo. If you believe documentation is missing (in source code or elsewhere), it should probably be there.