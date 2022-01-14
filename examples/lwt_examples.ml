open Lwt

let sleep_eg () =
  Lwt_unix.sleep 0.00001 >>= fun () ->
    Lwt_unix.sleep 1.0

let run_lwt () =
  print_endline "run_lwt";
  for i = 0 to 3 do
    ignore (i);
    Lwt_main.run (sleep_eg ())
  done;
  print_endline "done run_lwt";
