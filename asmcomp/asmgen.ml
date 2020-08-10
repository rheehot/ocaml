(**************************************************************************)
(*                                                                        *)
(*                                 OCaml                                  *)
(*                                                                        *)
(*             Xavier Leroy, projet Cristal, INRIA Rocquencourt           *)
(*                                                                        *)
(*   Copyright 1996 Institut National de Recherche en Informatique et     *)
(*     en Automatique.                                                    *)
(*                                                                        *)
(*   All rights reserved.  This file is distributed under the terms of    *)
(*   the GNU Lesser General Public License version 2.1, with the          *)
(*   special exception on linking described in the file LICENSE.          *)
(*                                                                        *)
(**************************************************************************)

(* From lambda to assembly code *)

[@@@ocaml.warning "+a-4-9-40-41-42"]

open Format
open Config
open Clflags
open Misc
open Cmm

type error = Assembler_error of string

exception Error of error

let liveness phrase = Liveness.fundecl phrase; phrase

let dump_if ppf flag message phrase =
  if !flag then Printmach.phase message ppf phrase

let pass_dump_if ppf flag message phrase =
  dump_if ppf flag message phrase; phrase

let pass_dump_linear_if ppf flag message phrase =
  if !flag then fprintf ppf "*** %s@.%a@." message Printlinear.fundecl phrase;
  phrase

let should_save_before_emit () =
  should_save_ir_after Compiler_pass.Scheduling

let linear_unit_info =
  { Linear_format.unit_name = "";
    items = [];
  }

let reset () =
  if should_save_before_emit () then begin
    linear_unit_info.unit_name <- Compilenv.current_unit_name ();
    linear_unit_info.items <- [];
  end

let save_data dl =
  if should_save_before_emit () then begin
    linear_unit_info.items <- Linear_format.(Data dl) :: linear_unit_info.items
  end;
  dl

let save_linear f =
  if should_save_before_emit () then begin
    linear_unit_info.items <- Linear_format.(Func f) :: linear_unit_info.items
  end;
  f

let write_linear output_prefix =
  if should_save_before_emit () then begin
    let filename = output_prefix ^ Clflags.Compiler_ir.(extension Linear) in
    linear_unit_info.items <- List.rev linear_unit_info.items;
    Linear_format.save filename linear_unit_info
  end

let should_emit () =
  not (should_stop_after Compiler_pass.Scheduling)

let if_emit_do f x = if should_emit () then f x else ()
let emit_begin_assembly = if_emit_do Emit.begin_assembly
let emit_end_assembly = if_emit_do Emit.end_assembly
let emit_data = if_emit_do Emit.data
let emit_fundecl =
  if_emit_do
    (Profile.record ~accumulate:true "emit" Emit.fundecl)

let rec regalloc ~ppf_dump round fd =
  if round > 50 then
    fatal_error(fd.Mach.fun_name ^
                ": function too complex, cannot complete register allocation");
  dump_if ppf_dump dump_live "Liveness analysis" fd;
  let num_stack_slots =
    if !use_linscan then begin
      (* Linear Scan *)
      Interval.build_intervals fd;
      if !dump_interval then Printmach.intervals ppf_dump ();
      Linscan.allocate_registers()
    end else begin
      (* Graph Coloring *)
      Interf.build_graph fd;
      if !dump_interf then Printmach.interferences ppf_dump ();
      if !dump_prefer then Printmach.preferences ppf_dump ();
      Coloring.allocate_registers()
    end
  in
  dump_if ppf_dump dump_regalloc "After register allocation" fd;
  let (newfd, redo_regalloc) = Reload.fundecl fd num_stack_slots in
  dump_if ppf_dump dump_reload "After insertion of reloading code" newfd;
  if redo_regalloc then begin
    Reg.reinit(); Liveness.fundecl newfd; regalloc ~ppf_dump (round + 1) newfd
  end else newfd

let (++) x f = f x

let compile_fundecl ~ppf_dump fd_cmm =
  Proc.init ();
  Reg.reset();
  fd_cmm
  ++ Profile.record ~accumulate:true "selection" Selection.fundecl
  ++ pass_dump_if ppf_dump dump_selection "After instruction selection"
  ++ Profile.record ~accumulate:true "comballoc" Comballoc.fundecl
  ++ pass_dump_if ppf_dump dump_combine "After allocation combining"
  ++ Profile.record ~accumulate:true "cse" CSE.fundecl
  ++ pass_dump_if ppf_dump dump_cse "After CSE"
  ++ Profile.record ~accumulate:true "liveness" liveness
  ++ Profile.record ~accumulate:true "deadcode" Deadcode.fundecl
  ++ pass_dump_if ppf_dump dump_live "Liveness analysis"
  ++ Profile.record ~accumulate:true "spill" Spill.fundecl
  ++ Profile.record ~accumulate:true "liveness" liveness
  ++ pass_dump_if ppf_dump dump_spill "After spilling"
  ++ Profile.record ~accumulate:true "split" Split.fundecl
  ++ pass_dump_if ppf_dump dump_split "After live range splitting"
  ++ Profile.record ~accumulate:true "liveness" liveness
  ++ Profile.record ~accumulate:true "regalloc" (regalloc ~ppf_dump 1)
  ++ Profile.record ~accumulate:true "available_regs" Available_regs.fundecl
  ++ Profile.record ~accumulate:true "linearize" Linearize.fundecl
  ++ pass_dump_linear_if ppf_dump dump_linear "Linearized code"
  ++ Profile.record ~accumulate:true "scheduling" Scheduling.fundecl
  ++ pass_dump_linear_if ppf_dump dump_scheduling "After instruction scheduling"
  ++ save_linear
  ++ emit_fundecl

let compile_data dl =
  dl
  ++ save_data
  ++ emit_data

let compile_phrase ~ppf_dump p =
  if !dump_cmm then fprintf ppf_dump "%a@." Printcmm.phrase p;
  match p with
  | Cfunction fd -> compile_fundecl ~ppf_dump fd
  | Cdata dl -> compile_data dl


(* For the native toplevel: generates generic functions unless
   they are already available in the process *)
let compile_genfuns ~ppf_dump f =
  List.iter
    (function
       | (Cfunction {fun_name = name}) as ph when f name ->
           compile_phrase ~ppf_dump ph
       | _ -> ())
    (Cmm_helpers.generic_functions true [Compilenv.current_unit_infos ()])

let compile_unit ~output_prefix ~asm_filename ~keep_asm ~obj_filename gen =
  reset ();
  let create_asm = should_emit () &&
                   (keep_asm || not !Emitaux.binary_backend_available) in
  Emitaux.create_asm_file := create_asm;
  Misc.try_finally
    ~exceptionally:(fun () -> remove_file obj_filename)
    (fun () ->
       if create_asm then Emitaux.output_channel := open_out asm_filename;
       Misc.try_finally
         (fun () ->
            gen ();
            write_linear output_prefix)
         ~always:(fun () ->
             if create_asm then close_out !Emitaux.output_channel)
         ~exceptionally:(fun () ->
             if create_asm && not keep_asm then remove_file asm_filename);
       if should_emit () then begin
         let assemble_result =
           Profile.record "assemble"
             (Proc.assemble_file asm_filename) obj_filename
         in
         if assemble_result <> 0
         then raise(Error(Assembler_error asm_filename));
       end;
       if create_asm && not keep_asm then remove_file asm_filename
    )

let end_gen_implementation ?toplevel ~ppf_dump to_cmm program =
  emit_begin_assembly ();
  program
  ++ Profile.record "cmm" to_cmm
  ++ Profile.record "compile_phrases" (List.iter (compile_phrase ~ppf_dump))
  ++ (fun () -> ());
  (match toplevel with None -> () | Some f -> compile_genfuns ~ppf_dump f);
  (* We add explicit references to external primitive symbols.  This
     is to ensure that the object files that define these symbols,
     when part of a C library, won't be discarded by the linker.
     This is important if a module that uses such a symbol is later
     dynlinked. *)
  compile_phrase ~ppf_dump
    (Cmm_helpers.reference_symbols
       (List.filter_map (fun prim ->
           if not (Primitive.native_name_is_external prim) then None
           else Some (Primitive.native_name prim))
          !Translmod.primitive_declarations));
  emit_end_assembly ()

type middle_end =
     backend:(module Backend_intf.S)
  -> filename:string
  -> prefixname:string
  -> ppf_dump:Format.formatter
  -> Lambda.program
  -> Clambda.with_constants

let asm_filename output_prefix =
    if !keep_asm_file || !Emitaux.binary_backend_available
    then output_prefix ^ ext_asm
    else Filename.temp_file "camlasm" ext_asm

let compile_implementation ?toplevel ~backend ~filename ~prefixname ~middle_end
      ~ppf_dump (program : Lambda.program) =
  compile_unit ~output_prefix:prefixname
    ~asm_filename:(asm_filename prefixname) ~keep_asm:!keep_asm_file
    ~obj_filename:(prefixname ^ ext_obj)
    (fun () ->
      Ident.Set.iter Compilenv.require_global program.required_globals;
      let clambda_with_constants =
        middle_end ~backend ~filename ~prefixname ~ppf_dump program
      in
      end_gen_implementation ?toplevel ~ppf_dump Cmmgen.compunit
        clambda_with_constants)

type middle_end_flambda =
     ppf_dump:Format.formatter
  -> prefixname:string
  -> backend:(module Flambda_backend_intf.S)
  -> filename:string
  -> module_ident:Ident.t
  -> module_block_size_in_words:int
  -> module_initializer:Lambda.lambda
  -> Flambda_middle_end.middle_end_result

let compile_implementation_flambda ?toplevel ~backend ~filename ~prefixname
    ~size:module_block_size_in_words ~module_ident ~module_initializer
    ~middle_end ~ppf_dump ~required_globals () =
  compile_unit ~output_prefix:prefixname
    ~asm_filename:(asm_filename prefixname) ~keep_asm:!keep_asm_file
    ~obj_filename:(prefixname ^ ext_obj)
    (fun () ->
      Ident.Set.iter Compilenv.require_global required_globals;
      let translated_program =
        (middle_end : middle_end_flambda) ~backend ~module_block_size_in_words
          ~filename ~prefixname ~ppf_dump ~module_ident ~module_initializer
      in
      end_gen_implementation ?toplevel ~ppf_dump Un_cps.unit
        translated_program)

let compile_implementation_flambda_for_ilambdac ?toplevel ~prefixname
    ~ppf_dump ~required_globals program =
  compile_unit ~output_prefix:prefixname
    ~asm_filename:(asm_filename prefixname) ~keep_asm:!keep_asm_file
    ~obj_filename:(prefixname ^ ext_obj)
    (fun () ->
      Ident.Set.iter Compilenv.require_global required_globals;
      end_gen_implementation ?toplevel ~ppf_dump
        Un_cps.unit program)
let linear_gen_implementation filename =
  let open Linear_format in
  let linear_unit_info, _ = restore filename in
  let emit_item = function
    | Data dl -> emit_data dl
    | Func f -> emit_fundecl f
  in
  emit_begin_assembly ();
  Profile.record "Emit" (List.iter emit_item) linear_unit_info.items;
  emit_end_assembly ()

let compile_implementation_linear output_prefix ~progname =
  compile_unit ~output_prefix
    ~asm_filename:(asm_filename output_prefix) ~keep_asm:!keep_asm_file
    ~obj_filename:(output_prefix ^ ext_obj)
    (fun () ->
      linear_gen_implementation progname)

module Flambda_backend = struct
  let symbol_for_module_block id =
    assert (Ident.global id);
    assert (not (Ident.is_predef id));
    let comp_unit =
      Compilenv.unit_for_global id
    in
    Symbol.unsafe_create
      comp_unit
      (Linkage_name.create (Compilenv.symbol_for_global id))

  let symbol_for_global' ?comp_unit id =
    if Ident.global id && not (Ident.is_predef id) then
      symbol_for_module_block id
    else
      let comp_unit =
        match comp_unit with
        | Some comp_unit -> comp_unit
        | None ->
          if Ident.is_predef id then Compilation_unit.predefined_exception ()
          else Compilation_unit.get_current_exn ()
      in
      Symbol.unsafe_create
        comp_unit
        (Linkage_name.create (Compilenv.symbol_for_global id))

  let closure_symbol _ = failwith "Not yet implemented"

  let division_by_zero =
    symbol_for_global'
      ~comp_unit:(Compilation_unit.predefined_exception ())
      Predef.ident_division_by_zero

  let invalid_argument =
    symbol_for_global'
      ~comp_unit:(Compilation_unit.predefined_exception ())
      Predef.ident_invalid_argument

  let all_predefined_exception_symbols =
    Predef.all_predef_exns
    |> List.map (fun ident ->
      symbol_for_global' ~comp_unit:(Compilation_unit.predefined_exception ())
        ident)
    |> Symbol.Set.of_list

  let () =
    assert (Symbol.Set.mem division_by_zero all_predefined_exception_symbols);
    assert (Symbol.Set.mem invalid_argument all_predefined_exception_symbols)

  let symbol_for_global' id : Symbol.t = symbol_for_global' id

  let size_int = Arch.size_int
  let big_endian = Arch.big_endian

  let max_sensible_number_of_arguments =
    Proc.max_arguments_for_tailcalls - 1

  let set_global_info info = Compilenv.set_global_info (Flambda (Some info))

  let get_global_info comp_unit =
    (* The Flambda simplifier should have returned the typing information
       for the predefined exception compilation unit before getting here. *)
    assert (not (Compilation_unit.is_predefined_exception comp_unit));
    if Compilation_unit.is_external_symbols comp_unit then None
    else
      let id =
        (* CR mshinwell: Unsure how to construct this properly.  Also see CR
          in Closure_conversion about the linkage names of module blocks *)
        Compilation_unit.get_persistent_ident comp_unit
      in
      match Compilenv.get_global_info' id with
      | None | Some (Flambda None) -> None
      | Some (Flambda (Some info)) -> Some info
      | Some (Clambda _) ->
        (* CR mshinwell: This should be a user error, not a fatal error. *)
        Misc.fatal_errorf "The .cmx file for unit %a was compiled with \
            the Closure middle-end, not Flambda, and cannot be loaded"
          Compilation_unit.print comp_unit
end

(* Error report *)

let report_error ppf = function
  | Assembler_error file ->
      fprintf ppf "Assembler error, input left in file %a"
        Location.print_filename file

let () =
  Location.register_error_of_exn
    (function
      | Error err -> Some (Location.error_of_printer_file report_error err)
      | _ -> None
    )
