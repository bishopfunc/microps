#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"
#include "util.h"
#include "net.h"

struct irq_entry {
    struct irq_entry *next;
    unsigned int irq;
    int (*handler)(unsigned int irq, void *dev);
    //割り込みが発生した際に呼び出す関数へのポインタ
    int flags;
    char name[16]; //識別名
    void *dev; //デバイス、色んなやつに対応するためvoid*で保持
};
/* NOTE: if you want to add/delete the entries after intr_run(), you need to protect these lists with a mutex. */

static struct irq_entry *irqs; //IRQリスト
//なぜリスト

static sigset_t sigmask; //シグナル集合

static pthread_t tid;
static pthread_barrier_t barrier;


int
intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev)
{
    struct irq_entry *entry;
    debugf("irq=%u, flags=%d, name=%s", irq, flags, name);
    for(entry = irqs; entry; entry = entry->next){
        if (entry->irq == irq) {
            if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED) {
              //両方の共有が許可されているかチェック XOR
                errorf("conflicts with already registered IRQs");
                return -1;
            }
        }
    }
    //メモリ確保 代入
    entry = memory_alloc(sizeof(*entry));
    if (!entry) {
        errorf("memory_alloc() failure");
        return -1;
    }
    entry->irq = irq;
    entry->handler = handler;
    entry->flags = flags;
    strncpy(entry->name, name, sizeof(entry->name)-1);
    //strncpy(list, list, len) 
    //配列に文字列をコピーする
    entry->dev = dev;
    entry->next = irqs;
    irqs = entry;
    sigaddset(&sigmask, irq);
    debugf("registered: irq=%u, name=%s", irq, name);

    return 0;
}

int
intr_raise_irq(unsigned int irq)
{
    return pthread_kill(tid, (int)irq);
}
//割り込みスレッドのスレッドID tid


static void *
intr_thread(void *arg)
{
    int terminate = 0, sig, err;
    struct irq_entry *entry;

    debugf("start...");
    pthread_barrier_wait(&barrier);
    while (!terminate) {
        err = sigwait(&sigmask, &sig);
        //sig?
        //シグナルが来るまで待機
        if (err) {
            errorf("sigwait() %s", strerror(err));
            break;
        }
        switch (sig) {
        case SIGHUP:
            terminate = 1;
            break;
            //ループを抜ける
        case SIGUSR1:
            net_softirq_handler(); //割り込みシグナル発生時に呼び出す
            break;
        default:
            for (entry = irqs; entry; entry = entry->next) {
                if (entry->irq == (unsigned int)sig) {
                  //なぜキャスト？
                  //IRQリストを巡回
                    debugf("irq=%d, name=%s", entry->irq, entry->name);
                    entry->handler(entry->irq, entry->dev);
                    //関数ポインタ？
                    //一致したら割り込みハンドラを呼び出す
                }
            }
            break;
        }
    }
    debugf("terminated");
    return NULL;
}


int
intr_run(void)
{
    int err;

    //シグナルマスクの設定
    err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (err) {
        errorf("pthread_sigmask() %s", strerror(err));
        return -1;
    }
    //割り込み処理スレッドの起動
    err = pthread_create(&tid, NULL, intr_thread, NULL);
    if (err) {
        errorf("pthread_create() %s", strerror(err));
        return -1;
    }
    //スレッドが動き出すまで待つ
    pthread_barrier_wait(&barrier);
    return 0;
}


void
intr_shutdown(void)
{
    //スレッドを起動済みかどうか
    if (pthread_equal(tid, pthread_self()) != 0) {
        /* Thread not created. */
        return;
    }
    //割り込み処理スレッドにシグナル（SIGHUP）を送信
    //??
    pthread_kill(tid, SIGHUP);
    //割り込み処理スレッドが完全に終了するのを待つ 
    //??
    pthread_join(tid, NULL);
}


int
intr_init(void)
{
    tid = pthread_self();//スレッドID設定
    pthread_barrier_init(&barrier, NULL, 2);
    //pthread_barrier の初期化（カウントを2に設定）
    sigemptyset(&sigmask);
    //シグナル集合を初期化（空にする）
    sigaddset(&sigmask, SIGHUP);
    //シグナル集合に SIGHUP を追加（割り込みスレッド終了通知用）
    sigaddset(&sigmask, SIGUSR1);//SIGUSR1を追加
    //????
    //ptheread
    return 0;
}
