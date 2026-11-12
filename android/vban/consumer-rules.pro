# JNI: native code constructs these by name / calls these signatures.
-keep class vban.sdk.VbanException { <init>(int, java.lang.String); }
-keep class vban.sdk.VbanDevice { <init>(...); }
-keepclasseswithmembernames class vban.sdk.VbanNative { native <methods>; }
