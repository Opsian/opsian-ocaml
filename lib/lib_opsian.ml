
(* Entrypoint into the native code *)
external start_opsian_native : string -> string -> string -> unit = "start_opsian_native"

let () =
  start_opsian_native (Sys.ocaml_version) (Sys.executable_name) (Sys.argv.(0))

external lwt_sample : unit -> bool = "lwt_sample"
external lwt_on_create : int -> unit = "lwt_on_create"
external lwt_on_resolve : int -> unit = "lwt_on_resolve"
external lwt_on_cancel : int -> unit = "lwt_on_cancel"
