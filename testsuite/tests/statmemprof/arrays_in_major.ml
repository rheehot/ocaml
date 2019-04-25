(* TEST
   flags = "-g"
   * bytecode
     reference = "${test_source_directory}/arrays_in_major.byte.reference"
   * native
     reference = "${test_source_directory}/arrays_in_major.opt.reference"
     compare_programs = "false"
*)

open Gc.Memprof

let root = ref []
let[@inline never] allocate_arrays lo hi cnt keep =
  for j = 0 to cnt-1 do
    for i = lo to hi do
      root := Array.make i 0 :: !root
    done;
    if not keep then root := []
  done

let check_nosample () =
  Printf.printf "check_nosample\n%!";
  start {
      sampling_rate = 0.;
      callstack_size = 10;
      callback = fun _ ->
        Printf.printf "Callback called with sampling_rate = 0\n";
        assert(false)
  };
  allocate_arrays 300 3000 1 false

let () = check_nosample ()

let check_ephe_full () =
  Printf.printf "check_ephe_full\n%!";
  let ephes = ref [] in
  start {
    sampling_rate = 0.01;
    callstack_size = 10;
    callback = fun _ ->
      let res = Ephemeron.K1.create () in
      ephes := res :: !ephes;
      Some res
  };
  allocate_arrays 300 3000 1 true;
  stop ();
  List.iter (fun e -> assert (Ephemeron.K1.check_key e)) !ephes;
  Gc.full_major ();
  List.iter (fun e -> assert (Ephemeron.K1.check_key e)) !ephes;
  root := [];
  Gc.full_major ();
  List.iter (fun e -> assert (not (Ephemeron.K1.check_key e))) !ephes

let () = check_ephe_full ()

let check_no_nested () =
  Printf.printf "check_no_nested\n%!";
  let in_callback = ref false in
  start {
      sampling_rate = 1.;
      callstack_size = 10;
      callback = fun _ ->
        assert (not !in_callback);
        in_callback := true;
        allocate_arrays 300 300 100 false;
        in_callback := false;
        None
  };
  allocate_arrays 300 300 100 false;
  stop ()

let () = check_no_nested ()

let check_distrib lo hi cnt rate =
  Printf.printf "check_distrib %d %d %d %f\n%!" lo hi cnt rate;
  let smp = ref 0 in
  start {
      sampling_rate = rate;
      callstack_size = 10;
      callback = fun info ->
        assert (info.kind = Major);
        assert (info.tag = 0);
        assert (info.size >= lo && info.size <= hi);
        assert (info.n_samples > 0);
        smp := !smp + info.n_samples;
        None
    };
  allocate_arrays lo hi cnt false;
  stop ();

  (* The probability distribution of the number of samples follows a
     poisson distribution with mean tot_alloc*rate. Given that we
     expect this quantity to be large (i.e., > 100), this distribution
     is approximately equal to a normal distribution. We compute a
     1e-6 confidence interval for !smp using quantiles of the normal
     distribution, and check that we are in this confidence interval. *)
  let tot_alloc = cnt*(lo+hi+2)*(hi-lo+1)/2 in
  let mean = float tot_alloc *. rate in
  let stddev = sqrt mean in
  (* This assertion has probability to fail close to 1e-6. *)
  assert (abs_float (mean -. float !smp) <= stddev *. 4.9)

let () =
  check_distrib 300 3000 1 0.00001;
  check_distrib 300 3000 1 0.0001;
  check_distrib 300 3000 1 0.01;
  check_distrib 300 3000 1 1.;
  check_distrib 300 300 100000 0.1;
  check_distrib 300000 300000 30 0.1

(* FIXME : in bytecode mode, the function [caml_get_current_callstack_impl],
   which is supposed to capture the current call stack, does not have access
   to the current value of [pc]. Therefore, depending on how the C call is
   performed, we may miss the first call stack slot in the captured backtraces.
   This is the reason why the reference file is different in native and
   bytecode modes.

   Note that [Printexc.get_callstack] does not suffer from this problem, because
   this function is actually an automatically generated stub which performs th
   C call. This is because [Printexc.get_callstack] is not declared as external
   in the mli file. *)

let[@inline never] check_callstack () =
  Printf.printf "check_callstack\n%!";
  let callstack = ref None in
  start {
      sampling_rate = 1.;
      callstack_size = 10;
      callback = fun info ->
        callstack := Some info.callstack;
        None
    };
  allocate_arrays 300 300 100 false;
  stop ();
  match !callstack with
  | None -> assert false
  | Some cs -> Printexc.print_raw_backtrace stdout cs

let () = check_callstack ()

let () =
  Printf.printf "OK !\n"
