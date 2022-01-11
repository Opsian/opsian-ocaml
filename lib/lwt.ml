
external lwt_sample : unit -> bool = "lwt_sample" [@@noalloc]
external lwt_on_create : int -> unit = "lwt_on_create" [@@noalloc]
external lwt_on_resolve : int -> unit = "lwt_on_resolve" [@@noalloc]
external lwt_on_cancel : int -> unit = "lwt_on_cancel" [@@noalloc]
