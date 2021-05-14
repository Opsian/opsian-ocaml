
let d n =
  n + 7
  [@@inline never]

let normal_c n =
  let n2 = ref 0 in
    for x = 0 to 1_000_000 do
      n2 := !n2 + n * d x
    done;
    !n2
  [@@inline never]

(* Function is always inlined in order to test out the debug information around inlining *)
(* TODO: do work that is always done, put a local variable behind an opaque identity *)
let inlined_c n =
  normal_c n
  [@@inline always]

let b n =
  let n2 = (inlined_c n) * (inlined_c n) in
    n2
    [@@inline never]

let a n =
  let n2 = (b n) * (b n) in
    n2
    [@@inline never]

let short_work () =
  for i = 1 to 10 do
    ignore(Sys.opaque_identity(a i));
    Thread.yield ()
  done

let do_work () =
  while true ; do
    ignore(Sys.opaque_identity(a 5));
    Thread.yield ()
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

let thread_cycler () =
  Printf.printf "Starting thread_cycler\n%!";
  let i = ref 1 in
  while true ; do
    let istr = string_of_int !i in
    let run () = print_endline ("starting thread " ^ istr); short_work (); print_endline ("done" ^ istr) in
    ignore (Thread.create run ());
    i := !i + 1;
    Unix.sleepf 0.001
  done

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
  | "thread_cycler"::[] -> thread_cycler ()
  | "work"::[] -> work ()
  | "sleep"::[] -> sleep ()
  | _ -> Printf.printf "Unknown \n";

