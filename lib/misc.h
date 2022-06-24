#ifndef OPSIAN_OCAML_MISC_H
#define OPSIAN_OCAML_MISC_H

#include <caml/version.h>

#if OCAML_VERSION_MAJOR == 5
    // Allow us to import misc.h by providing a definition for _Atomic
    // This isn't safe if we access any of the fields that use _Atomic directly, but we don't
    // This can be removed https://github.com/ocaml/ocaml/pull/11017 has been merged and propagates to opam
    #define _Atomic
#endif

#include <caml/misc.h> // CamlExtern

#endif //OPSIAN_OCAML_MISC_H
