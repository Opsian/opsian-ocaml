
(* Entrypoint into the native code *)
external start_opsian_native : unit -> unit = "start_opsian_native"

let () =
  print_endline "Starting Opsian ...";
  start_opsian_native ()

