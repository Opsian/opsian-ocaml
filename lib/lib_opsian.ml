
(* Entrypoint into the native code *)
external start_opsian_native : unit -> unit = "start_opsian_native"

let () =
  start_opsian_native ()

