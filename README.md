# opsian-ocaml
A low overhead, accurate, profiling library for the OCaml ecosystem


NB: remember to add:
git clone --recurse-submodules

## Eventring Metrics

Opsian supports ocaml event-ring metrics - these are currently experimental and haven't been merged into upstream Ocaml.
In order to use the event-ring metrics you need to use the eventring patchset

```
opam switch create eventring --empty
opam pin add ocaml-variants.4.12.0+eventring git+https://www.github.com/sadiqj/ocaml#eventring
```

