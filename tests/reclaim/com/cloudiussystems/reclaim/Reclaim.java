/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.reclaim;
import java.lang.Runtime;

public class Reclaim {

    static long memory = Runtime.getRuntime().maxMemory();

	public static void main(String[] args) {
        JVMConsumer jvm = new JVMConsumer();
        FSConsumer  fs  = new FSConsumer();

        System.out.println("Starting reclaimer test. Max Heap: " + memory);
        jvm.set_fs(fs);
        fs.set_jvm(jvm);

        jvm.alloc(95 * memory / 100);
        jvm.free_all();
        System.gc();
        System.out.println("Free Heap: " + Runtime.getRuntime().freeMemory());

        //System.out.println("Starting Warmup\n");
        //fs.warmup();
        //System.out.println("Warmup Done\n");

//        try {
//            Thread.sleep(4000);
//        } catch (InterruptedException e) {
//        }

        //System.out.println("Allocating 90 %...");
        //jvm.alloc(9 * memory / 10);
        //System.out.println("Reading back 90 %...");
        //fs.read(9 * memory / 10);


        for (int i = 1; i < 10; i++) {
            System.out.println("Iteration number " + i);
            jvm.alloc(i * memory / 10);
            fs.consume();
        }
	}
}
