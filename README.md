# minijson2

This is a small json parser (~350 loc for the source file) that I can use for my projects if I want to avoid dependencies.
I already made [pfirsich/minijson](https://github.com/pfirsich/minijson) in the past, but it was too slow to qualify for use in some of my projects. Because of some recurring build problems I wanted to replace [simdjson](https://github.com/simdjson/simdjson) in [pfirsich/gltf](https://github.com/pfirsich/gltf). 

minijson2 uses a [SAX](https://de.wikipedia.org/wiki/Simple_API_for_XML)-style parser (event-based) and an optional step to convert it to a [DOM](https://de.wikipedia.org/wiki/Document_Object_Model) on top. This is much faster than [pfirsich/minijson](https://github.com/pfirsich/minijson), but of course still massively slower than e.g. [simdjson](https://github.com/simdjson/simdjson).

## Allocations
With SAX-style parsing minijson2 will allocate almost no dynamic memory. Only a stack that remembers in which object the parser currently is is used. It does an initial allocation in the constructor of the parser and then additional allocations if the object depth of 512 is exceeded. I could use a fixed size array for the stack and avoid dynamic allocations all together, but I don't like artificial constraints like that and I think fixed size buffers are almost always trouble in the long run.
It's this reduction of memory allocations that makes minijson2 much faster than [pfirsich/minijson](https://github.com/pfirsich/minijson).

## Input
minijson2 will only parse from strings containing the whole input (no streams, files, etc). For my use cases (files of a few single-digit megabytes at most) reading the file into memory will not take long from an SSD and will not take up too much memory. Without this restriction it becomes massively more complicated to avoid allocations, because you need to store strings past a single parse step and the way I do it, you need to look ahead, effectively introducting a predefined maximum string length, etc. It's not worth it for me at the moment.

minijson2 will also escape strings in-place (optionally, but by default), which requires that the parser has a mutable reference to the input string. Consequently you should be careful parsing the same string multiple times.