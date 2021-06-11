
(* TODO: add programs to intermingle a combination, as well as FFI, multiple threads, blocking on locks, etc. *)

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

let do_work () =
  while true ; do
    ignore(Sys.opaque_identity(a 5))
  done

let work () =
  Printf.printf "Starting work\n%!";
  do_work ();
  Printf.printf "Done!\n%!"

let threads () =
  Printf.printf "Starting threads\n%!";
  let t1 = Thread.create (print_endline "t1"; do_work) ()
  and t2 = Thread.create (print_endline "t2"; do_work) () in
    Thread.join t1;
    Thread.join t2;
  Printf.printf "Done!\n%!"

let sleep () =
  Printf.printf "Starting sleep\n%!";
  Unix.sleep(30);
  Printf.printf "Done!\n%!"

let () =
  Printf.printf "Running example with:";
  Array.iter print_endline Sys.argv;
  print_endline "";

  match List.tl (Array.to_list Sys.argv) with
  [] -> work ()
  | "threads"::[] -> threads ()
  | "work"::[] -> work ()
  | "sleep"::[] -> sleep ()
  | _ -> Printf.printf "Unknown \n";

