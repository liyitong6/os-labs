#ifndef __KERN_MM_SLUB_TEST_H__
#define __KERN_MM_SLUB_TEST_H__

/* lab2 2413646: byte-layer self-check and test-only pre-init hook. */
void slub_check(void);
void slub_test_before_init(void);

#endif /* !__KERN_MM_SLUB_TEST_H__ */
