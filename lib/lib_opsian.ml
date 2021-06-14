
(* Entrypoint into the native code *)
external start_opsian_native : string -> string -> unit = "start_opsian_native"

let () =
  start_opsian_native (Sys.ocaml_version) (Sys.executable_name)
