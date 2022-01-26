(* Entrypoint into the native code *)
external start_opsian_native : string -> string -> string -> unit = "start_opsian_native"

let () =
  let (_ : Thread.t) = Thread.self () in
  start_opsian_native (Sys.ocaml_version) (Sys.executable_name) (Sys.argv.(0))
