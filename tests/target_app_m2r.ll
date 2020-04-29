; ModuleID = 'target_app_m2r.bc'
source_filename = "tests/target_app.bc"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.pointed_to = type { i32, i32 }
%struct.storage_struct = type { i32, %struct.pointed_to* }

@.str = private unnamed_addr constant [24 x i8] c"This is the string: %s\0A\00", align 1
@globalpointer = common global i8* null, align 8
@pointed = common global %struct.pointed_to zeroinitializer, align 4
@storage = common global %struct.storage_struct zeroinitializer, align 8
@copy_storage = common global %struct.storage_struct* null, align 8
@.str1 = private unnamed_addr constant [15 x i8] c"Pointer is %p\0A\00", align 1
@.str2 = private unnamed_addr constant [29 x i8] c"Pass this string to function\00", align 1

; Function Attrs: nounwind uwtable
define void @call_other_function(i8* %string) #0 {
entry:
  %call = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([24 x i8], [24 x i8]* @.str, i32 0, i32 0), i8* %string)
  %call1 = call noalias i8* @malloc(i64 4096) #3
  %add.ptr = getelementptr inbounds i8, i8* %call1, i64 4096
  store i8* %call1, i8** @globalpointer, align 8
  store %struct.pointed_to* @pointed, %struct.pointed_to** getelementptr inbounds (%struct.storage_struct, %struct.storage_struct* @storage, i32 0, i32 1), align 8
  store %struct.storage_struct* @storage, %struct.storage_struct** @copy_storage, align 8
  %call2 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @.str1, i32 0, i32 0), i8* %call1)
  ret void
}

declare i32 @printf(i8*, ...) #1

; Function Attrs: nounwind
declare noalias i8* @malloc(i64) #2

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  call void @call_other_function(i8* getelementptr inbounds ([29 x i8], [29 x i8]* @.str2, i32 0, i32 0))
  ret i32 0
}

attributes #0 = { nounwind uwtable "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.7.0 (trunk) (https://github.com/jtcriswell/llvm-dsa.git ad8a7d5819f35463224c483b3e1a62dc1cbcf966)"}
