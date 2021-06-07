
(* TODO: add programs to sleep, work and intermingle a combination, as well as FFI, multiple threads, blocking on locks, etc. *)

(*let () =
  print_endline "Starting sleep example";;
  Unix.sleep(30);;*)

let d n =
  (*Unix.sleepf 0.0101;*)
  n + 7
  [@@inline never]

let c n =
  let n2 = ref 0 in
    for x = 0 to 1_000_000 do
      n2 := !n2 + n * d x
    done;
    !n2
  [@@inline never]

let b n =
  let n2 = (c n) * (d n) in
    n2
    [@@inline never]

let a n =
  let n2 = (b n) * (b n) in
    n2
    [@@inline never]

let () =
  Printf.printf "Hello world!\n%!";
  while true ; do
    ignore(Sys.opaque_identity(a 5))
  done;
  Printf.printf "Done!\n%!"
