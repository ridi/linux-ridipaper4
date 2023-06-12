#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "gti5801"
struct gti_dev {
    int dev_major;
    struct device *dev;
    struct class *cls;
    struct i2c_client *client;
};

struct gti_dev *gti5801_dev;

int gti_drv_open(struct inode *inode, struct file *filp) {
    printk("--------^_*%s--------\n", __FUNCTION__);
    return 0;
}

ssize_t gti_drv_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    int ret;
    char *temp;
    printk("--------^_*%s--------\n", __FUNCTION__);

    //if (count <= 0 || count > 8192)
    if (count <= 0 || count > 20480)
        return -EINVAL;

    temp = kzalloc(count, GFP_KERNEL);

    ret = i2c_master_recv(gti5801_dev->client,temp,count);
    if (ret<0)
    {
        printk("gti_read_bytes error\n");
        goto err_0;

    }
    ret = copy_to_user(buf, temp, count);
    if (ret > 0)
    {
        printk("copy_to_user error");
        ret = -EFAULT;
        goto err_0;
    }

    kfree(temp);
    return count;
err_0:
    kfree(temp);
    return count;
}

ssize_t gti_drv_write(struct file *filp, const char __user *buf, size_t count, loff_t *fpos)
{
    int ret;
    char *temp;
    printk("--------^_*%s--------\n", __FUNCTION__);

    //if (count <= 0 || count > 8192)
    if (count <= 0 || count > 20480)
        return -EINVAL;

    temp = kzalloc(count, GFP_KERNEL);
    ret = copy_from_user(temp, buf,count);
    if (ret < 0)
    {
        printk("copy_to_user error");
        ret = -EFAULT;
        goto err_0;
    }
    ret = i2c_master_send(gti5801_dev->client,temp,count);
    if (ret<0)
    {
        printk("gti_write_bytes error\n");
        goto err_0;

    }

    kfree(temp);
    return count;
err_0:
    kfree(temp);
    return count;
}

int gti_drv_close(struct inode *inode, struct file *filp) {
    printk("--------^_*%s--------\n", __FUNCTION__);
    return 0;
}

static struct file_operations gti_fops = {
    .open = gti_drv_open,
    .read = gti_drv_read,
    .write = gti_drv_write,
    .release = gti_drv_close,
};

static int gti_drv_probe(struct i2c_client *client,
        const struct i2c_device_id *id) {
//    struct device *dev = &client->dev;
    struct device_node *np = client->dev.of_node;
    int gpio_ldo_num;

    printk("--------^_*%s--------\n", __FUNCTION__);
    gti5801_dev = kzalloc(sizeof(struct gti_dev), GFP_KERNEL);
    gti5801_dev->client = client;
    gti5801_dev->dev_major = register_chrdev(0, DRIVER_NAME, &gti_fops);
    gti5801_dev->cls = class_create(THIS_MODULE, "gti-cls");

    // /dev/gti5801
    // /sys/class/gti-cls/gti5801
    gti5801_dev->dev = device_create(gti5801_dev->cls, NULL,
            MKDEV(gti5801_dev->dev_major, 0), NULL, DRIVER_NAME);

    gpio_ldo_num = of_get_named_gpio(np, "spr5801_ldo_en", 0);
    printk("After getting gpio number %d\n", gpio_ldo_num);
    if (!gpio_is_valid(gpio_ldo_num)) {
            printk("%s failed to get a spr5801_ldo_en\n", __func__);
    }
    else {
            gpio_request(gpio_ldo_num, "spr5801_ldo_en");
            printk("ON: spr5801 ldo is %d", gpio_ldo_num);
            gpio_direction_output(gpio_ldo_num, 1);
            gpio_free(gpio_ldo_num);
    }
    return 0;
}
static int gti_drv_remove(struct i2c_client *client) {
    printk("--------^_*%s--------\n", __FUNCTION__);
    device_destroy(gti5801_dev->cls, MKDEV(gti5801_dev->dev_major, 0));
    class_destroy(gti5801_dev->cls);
    unregister_chrdev(gti5801_dev->dev_major, DRIVER_NAME);
    kfree(gti5801_dev);
    return 0;
}

static struct of_device_id gti_match_table[] = {
    { .compatible = "gti5801" },
    { }
};
static const struct i2c_device_id gti_id_table[] = { { DRIVER_NAME, 0 }, { } };
MODULE_DEVICE_TABLE( i2c, gti_id_table);

static struct i2c_driver gti_i2c_driver = {
    .driver =
    {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table =
            of_match_ptr(gti_match_table), },
    .probe = gti_drv_probe,
    .remove = gti_drv_remove,
    .id_table = gti_id_table,
};
static int __init gti_i2c_init(void)
{
    printk("--------^_*%s--------\n", __FUNCTION__);
    return i2c_add_driver(&gti_i2c_driver);
}
static void __exit gti_i2c_exit(void)
{
    printk("--------^_*%s--------\n", __FUNCTION__);
    i2c_del_driver(&gti_i2c_driver);
}

module_init( gti_i2c_init);
module_exit( gti_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hqzy");
MODULE_DESCRIPTION("gti_i2c driver");
