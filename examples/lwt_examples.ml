open Lwt

(* Definitions in the example project just to avoid creating an lwt specific library *)
let opsian_tracer : Lwt_sampling.tracer = {
  sample = Opsian.Lwt.lwt_sample;
  on_create = Opsian.Lwt.lwt_on_create;
  on_resolve = Opsian.Lwt.lwt_on_resolve;
  on_cancel = Opsian.Lwt.lwt_on_cancel;
}

let sleep_eg () =
  Lwt_unix.sleep 0.00001 >>= fun () ->
    Lwt_unix.sleep 1.0

let run_lwt () =
  (* Lwt seems to create some thunks before this even starts *)
  Lwt_sampling.current_tracer := opsian_tracer;
  print_endline "run_lwt";
  for i = 0 to 3 do
    ignore (i);
    Lwt_main.run (sleep_eg ())
  done;
  print_endline "done run_lwt";
