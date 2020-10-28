package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "96c03ef7-5e00-43b7-9353-2d0b02cfef3e";
  public static final String DEFAULT_CONT_ID = "4e3ce803-b4ef-4b26-8166-70189b729269";

  public static final String DEFAULT_OBJECT_CONT_ID = "0e2cc7c0-91e6-4810-957d-44b0c5df65f9";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
