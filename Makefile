_build/default/.gitignore: .gitignore
	mkdir -p _build/default; \
	cp .gitignore _build/default/.gitignore


_build/default/LICENSE: LICENSE
	mkdir -p _build/default; \
	cp LICENSE _build/default/LICENSE


_build/default/Makefile: Makefile
	mkdir -p _build/default; \
	cp Makefile _build/default/Makefile


_build/default/README.md: README.md
	mkdir -p _build/default; \
	cp README.md _build/default/README.md


_build/default/build.sh: build.sh
	mkdir -p _build/default; \
	cp build.sh _build/default/build.sh


_build/default/dune-project: dune-project
	mkdir -p _build/default; \
	cp dune-project _build/default/dune-project


_build/default/run-eg.sh: run-eg.sh
	mkdir -p _build/default; \
	cp run-eg.sh _build/default/run-eg.sh


_build/.aliases/default/all-00000000000000000000000000000000: \
  _build/default/.gitignore _build/default/LICENSE _build/default/Makefile \
  _build/default/README.md _build/default/build.sh \
  _build/default/dune-project _build/default/run-eg.sh
	mkdir -p _build/.aliases/default; \
	echo > _build/.aliases/default/all-00000000000000000000000000000000


_build/default/examples/.merlin-exist-exe-opsian_examples: \
  _build/default/examples/.merlin-conf/exe-opsian_examples
	mkdir -p _build/default/examples; \
	mkdir -p _build/default; \
	cd _build/default; \
	true > examples/.merlin-exist-exe-opsian_examples


_build/default/lib/.merlin-exist-lib-lib_opsian: \
  _build/default/lib/.merlin-conf/lib-lib_opsian
	mkdir -p _build/default/lib; \
	mkdir -p _build/default; \
	cd _build/default; \
	true > lib/.merlin-exist-lib-lib_opsian


_build/default/lib/lib_opsian.a _build/default/lib/lib_opsian.cmxa: \
  /home/richard/.opam/4.10.0/bin/ocamlopt.opt \
  _build/default/lib/.lib_opsian.objs/native/lib_opsian.cmx \
  _build/default/lib/.lib_opsian.objs/native/lib_opsian.o \
  _build/default/lib/.merlin-exist-lib-lib_opsian
	mkdir -p _build/default/lib; \
	mkdir -p _build/default; \
	cd _build/default; \
	/home/richard/.opam/4.10.0/bin/ocamlopt.opt -w \
	  @1..3@5..28@30..39@43@46..47@49..57@61..62-40 -strict-sequence \
	  -strict-formats -short-paths -keep-locs -g -a -o lib/lib_opsian.cmxa -cclib \
	  -llib_opsian_stubs -cclib -lunwind -linkall \
	  lib/.lib_opsian.objs/native/lib_opsian.cmx


_build/default/lib/another_c_file.c: lib/another_c_file.c
	mkdir -p _build/default/lib; \
	cp lib/another_c_file.c _build/default/lib/another_c_file.c


_build/default/lib/another_c_file.h: lib/another_c_file.h
	mkdir -p _build/default/lib; \
	cp lib/another_c_file.h _build/default/lib/another_c_file.h


_build/default/lib/another_c_file.o: /usr/bin/gcc \
  _build/default/lib/.merlin-exist-lib-lib_opsian \
  _build/default/lib/another_c_file.c _build/default/lib/another_c_file.h
	mkdir -p _build/default/lib; \
	mkdir -p _build/default/lib; \
	cd _build/default/lib; \
	/usr/bin/gcc -O2 -fno-strict-aliasing -fwrapv -fPIC -D_FILE_OFFSET_BITS=64 \
	  -D_REENTRANT -O2 -fno-strict-aliasing -fwrapv -fPIC -g -I \
	  /home/richard/.opam/4.10.0/lib/ocaml -o another_c_file.o -c \
	  another_c_file.c


_build/default/lib/lib_opsian.c: lib/lib_opsian.c
	mkdir -p _build/default/lib; \
	cp lib/lib_opsian.c _build/default/lib/lib_opsian.c


_build/default/lib/lib_opsian.o: /usr/bin/gcc \
  _build/default/lib/.merlin-exist-lib-lib_opsian \
  _build/default/lib/another_c_file.h _build/default/lib/lib_opsian.c
	mkdir -p _build/default/lib; \
	mkdir -p _build/default/lib; \
	cd _build/default/lib; \
	/usr/bin/gcc -O2 -fno-strict-aliasing -fwrapv -fPIC -D_FILE_OFFSET_BITS=64 \
	  -D_REENTRANT -O2 -fno-strict-aliasing -fwrapv -fPIC -g -I \
	  /home/richard/.opam/4.10.0/lib/ocaml -o lib_opsian.o -c lib_opsian.c


_build/default/lib/dlllib_opsian_stubs.so \
  _build/default/lib/liblib_opsian_stubs.a: \
  /home/richard/.opam/4.10.0/bin/ocamlmklib.opt \
  _build/default/lib/.merlin-exist-lib-lib_opsian \
  _build/default/lib/another_c_file.o _build/default/lib/lib_opsian.o
	mkdir -p _build/default/lib; \
	mkdir -p _build/default; \
	cd _build/default; \
	/home/richard/.opam/4.10.0/bin/ocamlmklib.opt -g -o lib/lib_opsian_stubs \
	  lib/another_c_file.o lib/lib_opsian.o -lunwind


_build/default/examples/opsian_examples.exe: \
  /home/richard/.opam/4.10.0/bin/ocamlopt.opt \
  _build/default/examples/.merlin-exist-exe-opsian_examples \
  _build/default/examples/.opsian_examples.eobjs/native/dune__exe__Opsian_examples.cmx \
  _build/default/examples/.opsian_examples.eobjs/native/dune__exe__Opsian_examples.o \
  _build/default/lib/lib_opsian.a _build/default/lib/lib_opsian.cmxa \
  _build/default/lib/liblib_opsian_stubs.a
	mkdir -p _build/default/examples; \
	mkdir -p _build/default; \
	cd _build/default; \
	/home/richard/.opam/4.10.0/bin/ocamlopt.opt -w \
	  @1..3@5..28@30..39@43@46..47@49..57@61..62-40 -strict-sequence \
	  -strict-formats -short-paths -keep-locs -g -o examples/opsian_examples.exe \
	  lib/lib_opsian.cmxa -I lib \
	  examples/.opsian_examples.eobjs/native/dune__exe__Opsian_examples.cmx


_build/default/examples/dune: examples/dune
	mkdir -p _build/default/examples; \
	cp examples/dune _build/default/examples/dune


_build/default/examples/opsian_examples.ml: examples/opsian_examples.ml
	mkdir -p _build/default/examples; \
	cp examples/opsian_examples.ml _build/default/examples/opsian_examples.ml


_build/.aliases/default/examples/all-00000000000000000000000000000000: \
  _build/default/examples/.merlin-exist-exe-opsian_examples \
  _build/default/examples/dune _build/default/examples/opsian_examples.exe \
  _build/default/examples/opsian_examples.ml
	mkdir -p _build/.aliases/default/examples; \
	echo > _build/.aliases/default/examples/all-00000000000000000000000000000000


_build/default/lib/lib_opsian.cma: /home/richard/.opam/4.10.0/bin/ocamlc.opt \
  _build/default/lib/.lib_opsian.objs/byte/lib_opsian.cmo \
  _build/default/lib/.merlin-exist-lib-lib_opsian
	mkdir -p _build/default/lib; \
	mkdir -p _build/default; \
	cd _build/default; \
	/home/richard/.opam/4.10.0/bin/ocamlc.opt -w \
	  @1..3@5..28@30..39@43@46..47@49..57@61..62-40 -strict-sequence \
	  -strict-formats -short-paths -keep-locs -g -a -o lib/lib_opsian.cma -dllib \
	  -llib_opsian_stubs -cclib -llib_opsian_stubs -cclib -lunwind -linkall \
	  lib/.lib_opsian.objs/byte/lib_opsian.cmo


_build/default/lib/lib_opsian.cmxs: \
  /home/richard/.opam/4.10.0/bin/ocamlopt.opt \
  _build/default/lib/.merlin-exist-lib-lib_opsian \
  _build/default/lib/lib_opsian.a _build/default/lib/lib_opsian.cmxa \
  _build/default/lib/liblib_opsian_stubs.a
	mkdir -p _build/default/lib; \
	mkdir -p _build/default; \
	cd _build/default; \
	/home/richard/.opam/4.10.0/bin/ocamlopt.opt -w \
	  @1..3@5..28@30..39@43@46..47@49..57@61..62-40 -strict-sequence \
	  -strict-formats -short-paths -keep-locs -g -shared -linkall -I lib -o \
	  lib/lib_opsian.cmxs lib/lib_opsian.cmxa


_build/default/lib/dune: lib/dune
	mkdir -p _build/default/lib; \
	cp lib/dune _build/default/lib/dune


_build/default/lib/lib_opsian.ml: lib/lib_opsian.ml
	mkdir -p _build/default/lib; \
	cp lib/lib_opsian.ml _build/default/lib/lib_opsian.ml


_build/.aliases/default/lib/all-00000000000000000000000000000000: \
  _build/default/lib/.merlin-exist-lib-lib_opsian \
  _build/default/lib/another_c_file.c _build/default/lib/another_c_file.h \
  _build/default/lib/another_c_file.o \
  _build/default/lib/dlllib_opsian_stubs.so _build/default/lib/dune \
  _build/default/lib/lib_opsian.a _build/default/lib/lib_opsian.c \
  _build/default/lib/lib_opsian.cma _build/default/lib/lib_opsian.cmxa \
  _build/default/lib/lib_opsian.cmxs _build/default/lib/lib_opsian.ml \
  _build/default/lib/lib_opsian.o _build/default/lib/liblib_opsian_stubs.a
	mkdir -p _build/.aliases/default/lib; \
	echo > _build/.aliases/default/lib/all-00000000000000000000000000000000


_build/.aliases/default/default-00000000000000000000000000000000: \
  _build/.aliases/default/all-00000000000000000000000000000000 \
  _build/.aliases/default/examples/all-00000000000000000000000000000000 \
  _build/.aliases/default/lib/all-00000000000000000000000000000000
	mkdir -p _build/.aliases/default; \
	echo > _build/.aliases/default/default-00000000000000000000000000000000


_build/.aliases/default/doc-private-00000000000000000000000000000000: \
  _build/.aliases/default/_doc/_html/lib_opsian@26bb1931b3ad/doc-00000000000000000000000000000000
	mkdir -p _build/.aliases/default; \
	echo > _build/.aliases/default/doc-private-00000000000000000000000000000000


_build/.aliases/default/fmt-00000000000000000000000000000000: \
  _build/.aliases/default/.formatted/fmt-00000000000000000000000000000000
	mkdir -p _build/.aliases/default; \
	echo > _build/.aliases/default/fmt-00000000000000000000000000000000


_build/.aliases/default/examples/check-00000000000000000000000000000000: \
  _build/default/examples/.merlin-conf/exe-opsian_examples \
  _build/default/examples/.opsian_examples.eobjs/byte/dune__exe__Opsian_examples.cmi \
  _build/default/examples/.opsian_examples.eobjs/byte/dune__exe__Opsian_examples.cmt
	mkdir -p _build/.aliases/default/examples; \
	echo > \
	  _build/.aliases/default/examples/check-00000000000000000000000000000000


_build/.aliases/default/examples/default-00000000000000000000000000000000: \
  _build/.aliases/default/examples/all-00000000000000000000000000000000
	mkdir -p _build/.aliases/default/examples; \
	echo > \
	  _build/.aliases/default/examples/default-00000000000000000000000000000000


_build/.aliases/default/examples/fmt-00000000000000000000000000000000: \
  _build/.aliases/default/examples/.formatted/fmt-00000000000000000000000000000000
	mkdir -p _build/.aliases/default/examples; \
	echo > _build/.aliases/default/examples/fmt-00000000000000000000000000000000


_build/.aliases/default/lib/check-00000000000000000000000000000000: \
  _build/default/lib/.lib_opsian.objs/byte/lib_opsian.cmi \
  _build/default/lib/.lib_opsian.objs/byte/lib_opsian.cmt \
  _build/default/lib/.merlin-conf/lib-lib_opsian \
  _build/default/lib/another_c_file.o _build/default/lib/lib_opsian.o
	mkdir -p _build/.aliases/default/lib; \
	echo > _build/.aliases/default/lib/check-00000000000000000000000000000000


_build/.aliases/default/lib/default-00000000000000000000000000000000: \
  _build/.aliases/default/lib/all-00000000000000000000000000000000
	mkdir -p _build/.aliases/default/lib; \
	echo > _build/.aliases/default/lib/default-00000000000000000000000000000000


_build/.aliases/default/lib/fmt-00000000000000000000000000000000: \
  _build/.aliases/default/lib/.formatted/fmt-00000000000000000000000000000000
	mkdir -p _build/.aliases/default/lib; \
	echo > _build/.aliases/default/lib/fmt-00000000000000000000000000000000

