(* BEGIN LWT Specific Code *)

(* let opsian_tracer : Lwt_sampling.tracer = {
  sample = Lwt.lwt_sample;
  on_create = Lwt.lwt_on_create;
  on_resolve = Lwt.lwt_on_resolve;
  on_cancel = Lwt.lwt_on_cancel;
} *)

(* END LWT Specific Code *)

(* Entrypoint into the native code *)
external start_opsian_native : string -> string -> string -> unit = "start_opsian_native"

let () =
  (* Lwt_sampling.current_tracer := opsian_tracer; *)
  start_opsian_native (Sys.ocaml_version) (Sys.executable_name) (Sys.argv.(0))
