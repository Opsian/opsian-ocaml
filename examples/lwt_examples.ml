open Lwt

let sleep_eg () =
  Lwt_unix.sleep 0.1 >>= fun () ->
    Lwt_unix.sleep 0.5

let op1 () = Lwt_unix.sleep 0.1
let op2 () = Lwt_unix.sleep 0.1
let op3 () = Lwt_unix.sleep 0.1

let run_joined () =
  Lwt.join [ op1() ; op2() ; op3()]

let run_choose () =
  Lwt.choose [ op1() ; op2() ; op3()]

let run_lwt f =
  print_endline "run_lwt";
  for i = 0 to 3 do
    ignore (i);
    Lwt_main.run (f ())
  done;
  print_endline "done"

let run_lwt_sleep () = run_lwt sleep_eg
let run_lwt_join () = run_lwt run_joined
let run_lwt_choose () = run_lwt run_choose
