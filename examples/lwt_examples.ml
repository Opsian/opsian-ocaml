open Lwt

let [@inline never][@local never][@specialise never] op1 () =
  ignore(Sys.opaque_identity(1));
  Lwt_unix.sleep 0.1

let [@inline never][@local never][@specialise never] op2 () =
  ignore(Sys.opaque_identity(1));
  Lwt_unix.sleep 0.2

let [@inline never][@local never][@specialise never] op3 () =
  ignore(Sys.opaque_identity(1));
  Lwt_unix.sleep 0.3

let [@inline never][@local never][@specialise never] sleep_eg () =
  op1 () >>=
    op2

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
